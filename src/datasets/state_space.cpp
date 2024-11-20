/*
 * Copyright (C) 2023 Simon Stahlberg
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "mimir/datasets/state_space.hpp"

#include "mimir/algorithms/BS_thread_pool.hpp"
#include "mimir/common/timers.hpp"
#include "mimir/graphs/static_graph_boost_adapter.hpp"

#include <algorithm>
#include <cstdlib>
#include <deque>

namespace mimir
{
/**
 * StateSpace
 */
StateSpace::StateSpace(Problem problem,
                       bool use_unit_cost_one,
                       std::shared_ptr<PDDLRepositories> pddl_repositories,
                       std::shared_ptr<IApplicableActionGenerator> applicable_action_generator,
                       std::shared_ptr<StateRepository> state_repository,
                       typename StateSpace::GraphType graph,
                       StateMap<Index> state_to_index,
                       Index initial_state,
                       IndexSet goal_states,
                       IndexSet deadend_states,
                       ContinuousCostList goal_distances) :
    m_problem(problem),
    m_use_unit_cost_one(use_unit_cost_one),
    m_pddl_repositories(std::move(pddl_repositories)),
    m_applicable_action_generator(std::move(applicable_action_generator)),
    m_state_repository(std::move(state_repository)),
    m_graph(std::move(graph)),
    m_state_to_index(std::move(state_to_index)),
    m_initial_state(initial_state),
    m_goal_states(std::move(goal_states)),
    m_deadend_states(std::move(deadend_states)),
    m_goal_distances(std::move(goal_distances)),
    m_states_by_goal_distance()
{
    for (size_t state_index = 0; state_index < m_graph.get_num_vertices(); ++state_index)
    {
        m_states_by_goal_distance[m_goal_distances.at(state_index)].push_back(state_index);
    }
}

std::optional<StateSpace> StateSpace::create(Problem problem, const std::shared_ptr<PDDLRepositories>& factories, StateSpaceOptions options)
{
    auto aag = std::make_shared<GroundedApplicableActionGenerator>(problem, factories);
    auto ssg = std::make_shared<StateRepository>(aag);
    return StateSpace::create(problem, factories, aag, std::move(ssg), options);
}

std::optional<StateSpace> StateSpace::create(const fs::path& domain_filepath, const fs::path& problem_filepath, const StateSpaceOptions& options)
{
    auto parser = PDDLParser(domain_filepath, problem_filepath);
    auto applicable_action_generator = std::make_shared<GroundedApplicableActionGenerator>(parser.get_problem(), parser.get_pddl_repositories());
    auto state_repository = std::make_shared<StateRepository>(applicable_action_generator);
    return StateSpace::create(parser.get_problem(), parser.get_pddl_repositories(), applicable_action_generator, state_repository, options);
}

std::optional<StateSpace> StateSpace::create(Problem problem,
                                             std::shared_ptr<PDDLRepositories> factories,
                                             std::shared_ptr<IApplicableActionGenerator> applicable_action_generator,
                                             std::shared_ptr<StateRepository> state_repository,
                                             const StateSpaceOptions& options)
{
    auto stop_watch = StopWatch(options.timeout_ms);

    auto initial_state = state_repository->get_or_create_initial_state();

    if (options.remove_if_unsolvable && !problem->static_goal_holds())
    {
        // Unsolvable
        return std::nullopt;
    }

    auto graph = StaticGraph<StateVertex, GroundActionEdge>();
    const auto initial_state_index = graph.add_vertex(State(initial_state));
    auto goal_states = IndexSet {};
    auto state_to_index = StateMap<Index> {};
    state_to_index.emplace(initial_state, initial_state_index);

    auto lifo_queue = std::deque<StateVertex>();
    lifo_queue.push_back(graph.get_vertices().at(initial_state_index));

    auto applicable_actions = GroundActionList {};
    stop_watch.start();
    while (!lifo_queue.empty() && !stop_watch.has_finished())
    {
        const auto state = lifo_queue.back();
        const auto state_index = state.get_index();
        lifo_queue.pop_back();
        if (mimir::get_state(state)->literals_hold(problem->get_goal_condition<Fluent>())
            && mimir::get_state(state)->literals_hold(problem->get_goal_condition<Derived>()))
        {
            goal_states.insert(state_index);
        }

        applicable_action_generator->generate_applicable_actions(mimir::get_state(state), applicable_actions);
        for (const auto& action : applicable_actions)
        {
            const auto successor_state = state_repository->get_or_create_successor_state(mimir::get_state(state), action);
            const auto it = state_to_index.find(successor_state);
            const bool exists = (it != state_to_index.end());
            if (exists)
            {
                const auto successor_state_index = it->second;
                graph.add_directed_edge(state_index, successor_state_index, action);
                continue;
            }

            const auto successor_state_index = graph.add_vertex(successor_state);
            if (successor_state_index >= options.max_num_states)
            {
                // Ran out of state resources
                return std::nullopt;
            }

            graph.add_directed_edge(state_index, successor_state_index, action);
            state_to_index.emplace(successor_state, successor_state_index);
            lifo_queue.push_back(graph.get_vertices().at(successor_state_index));
        }
    }

    if (stop_watch.has_finished())
    {
        // Ran out of time
        return std::nullopt;
    }

    if (options.remove_if_unsolvable && goal_states.empty())
    {
        // Skip: unsolvable
        return std::nullopt;
    }

    auto bidirectional_graph = typename StateSpace::GraphType(std::move(graph));

    auto goal_distances = ContinuousCostList {};
    if (options.use_unit_cost_one
        || std::all_of(bidirectional_graph.get_edges().begin(),
                       bidirectional_graph.get_edges().end(),
                       [](const auto& transition) { return get_cost(transition) == 1; }))
    {
        auto [predecessors_, goal_distances_] =
            breadth_first_search(TraversalDirectionTaggedType(bidirectional_graph, BackwardTraversal()), goal_states.begin(), goal_states.end());
        goal_distances = std::move(goal_distances_);
    }
    else
    {
        auto transition_costs = ContinuousCostList {};
        transition_costs.reserve(bidirectional_graph.get_num_edges());
        for (const auto& transition : bidirectional_graph.get_edges())
        {
            transition_costs.push_back(get_cost(transition));
        }
        auto [predecessors_, goal_distances_] = dijkstra_shortest_paths(TraversalDirectionTaggedType(bidirectional_graph, BackwardTraversal()),
                                                                        transition_costs,
                                                                        goal_states.begin(),
                                                                        goal_states.end());
        goal_distances = std::move(goal_distances_);
    }

    auto deadend_states = IndexSet {};
    for (Index state_index = 0; state_index < bidirectional_graph.get_num_vertices(); ++state_index)
    {
        if (goal_distances.at(state_index) == std::numeric_limits<ContinuousCost>::infinity())
        {
            deadend_states.insert(state_index);
        }
    }

    return StateSpace(problem,
                      options.use_unit_cost_one,
                      std::move(factories),
                      std::move(applicable_action_generator),
                      std::move(state_repository),
                      std::move(bidirectional_graph),
                      std::move(state_to_index),
                      initial_state_index,
                      std::move(goal_states),
                      std::move(deadend_states),
                      std::move(goal_distances));
}

StateSpaceList StateSpace::create(const fs::path& domain_filepath, const std::vector<fs::path>& problem_filepaths, const StateSpacesOptions& options)
{
    auto memories =
        std::vector<std::tuple<Problem, std::shared_ptr<PDDLRepositories>, std::shared_ptr<IApplicableActionGenerator>, std::shared_ptr<StateRepository>>> {};
    for (const auto& problem_filepath : problem_filepaths)
    {
        auto parser = PDDLParser(domain_filepath, problem_filepath);
        auto applicable_action_generator = std::make_shared<GroundedApplicableActionGenerator>(parser.get_problem(), parser.get_pddl_repositories());
        auto state_repository = std::make_shared<StateRepository>(applicable_action_generator);
        memories.emplace_back(parser.get_problem(), parser.get_pddl_repositories(), applicable_action_generator, state_repository);
    }

    return StateSpace::create(memories, options);
}

std::vector<StateSpace> StateSpace::create(
    const std::vector<std::tuple<Problem, std::shared_ptr<PDDLRepositories>, std::shared_ptr<IApplicableActionGenerator>, std::shared_ptr<StateRepository>>>&
        memories,
    const StateSpacesOptions& options)
{
    auto state_spaces = StateSpaceList {};
    auto pool = BS::thread_pool(options.num_threads);
    auto futures = std::vector<std::future<std::optional<StateSpace>>> {};

    for (const auto& [problem, factories, applicable_action_generator, state_repository] : memories)
    {
        futures.push_back(
            pool.submit_task([problem, factories, applicable_action_generator, state_repository, state_space_options = options.state_space_options]
                             { return StateSpace::create(problem, factories, applicable_action_generator, state_repository, state_space_options); }));
    }

    for (auto& future : futures)
    {
        auto state_space = future.get();
        if (state_space.has_value())
        {
            state_spaces.push_back(std::move(state_space.value()));
        }
    }

    if (options.sort_ascending_by_num_states)
    {
        std::sort(state_spaces.begin(), state_spaces.end(), [](const auto& l, const auto& r) { return l.get_num_states() < r.get_num_states(); });
    }

    return state_spaces;
}

/**
 * Extended functionality
 */

template<IsTraversalDirection Direction>
ContinuousCostList StateSpace::compute_shortest_distances_from_states(const IndexList& states) const
{
    auto distances = ContinuousCostList {};
    if (m_use_unit_cost_one
        || std::all_of(m_graph.get_edges().begin(), m_graph.get_edges().end(), [](const auto& transition) { return get_cost(transition) == 1; }))
    {
        auto [predecessors_, distances_] = breadth_first_search(TraversalDirectionTaggedType(m_graph, Direction()), states.begin(), states.end());
        distances = std::move(distances_);
    }
    else
    {
        auto transition_costs = ContinuousCostList {};
        transition_costs.reserve(m_graph.get_num_edges());
        for (const auto& transition : m_graph.get_edges())
        {
            transition_costs.push_back(get_cost(transition));
        }
        auto [predecessors_, distances_] =
            dijkstra_shortest_paths(TraversalDirectionTaggedType(m_graph, Direction()), transition_costs, states.begin(), states.end());
        distances = std::move(distances_);
    }

    return distances;
}

template ContinuousCostList StateSpace::compute_shortest_distances_from_states<ForwardTraversal>(const IndexList& states) const;
template ContinuousCostList StateSpace::compute_shortest_distances_from_states<BackwardTraversal>(const IndexList& states) const;

template<IsTraversalDirection Direction>
ContinuousCostMatrix StateSpace::compute_pairwise_shortest_state_distances() const
{
    auto transition_costs = ContinuousCostList {};
    if (m_use_unit_cost_one)
    {
        transition_costs = ContinuousCostList(m_graph.get_num_edges(), 1);
    }
    else
    {
        transition_costs.reserve(m_graph.get_num_edges());
        for (const auto& transition : m_graph.get_edges())
        {
            transition_costs.push_back(get_cost(transition));
        }
    }

    return floyd_warshall_all_pairs_shortest_paths(TraversalDirectionTaggedType(m_graph, Direction()), transition_costs).get_matrix();
}

template ContinuousCostMatrix StateSpace::compute_pairwise_shortest_state_distances<ForwardTraversal>() const;
template ContinuousCostMatrix StateSpace::compute_pairwise_shortest_state_distances<BackwardTraversal>() const;

/**
 *  Getters
 */

/* Meta data */
Problem StateSpace::get_problem() const { return m_problem; }
bool StateSpace::get_use_unit_cost_one() const { return m_use_unit_cost_one; }

/* Memory */
const std::shared_ptr<PDDLRepositories>& StateSpace::get_pddl_repositories() const { return m_pddl_repositories; }

const std::shared_ptr<IApplicableActionGenerator>& StateSpace::get_applicable_action_generator() const { return m_applicable_action_generator; }

const std::shared_ptr<StateRepository>& StateSpace::get_state_repository() const { return m_state_repository; }

/* Graph */
const typename StateSpace::GraphType& StateSpace::get_graph() const { return m_graph; }

State StateSpace::get_state(Index state) const { return mimir::get_state(m_graph.get_vertices().at(state)); }

Index StateSpace::get_index(State state) const { return m_state_to_index.at(state); }

Index StateSpace::get_initial_state_index() const { return m_initial_state; }
State StateSpace::get_initial_state() const { return get_state(m_initial_state); }

const IndexSet& StateSpace::get_goal_state_indices() const { return m_goal_states; }

const IndexSet& StateSpace::get_deadend_state_indices() const { return m_deadend_states; }

size_t StateSpace::get_num_states() const { return m_graph.get_num_vertices(); }

size_t StateSpace::get_num_goal_states() const { return get_goal_state_indices().size(); }

size_t StateSpace::get_num_deadend_states() const { return get_deadend_state_indices().size(); }

bool StateSpace::is_goal_state(Index state) const { return get_goal_state_indices().count(state); }
bool StateSpace::is_goal_state(State state) const { return is_goal_state(get_index(state)); }

bool StateSpace::is_deadend_state(Index state) const { return get_deadend_state_indices().count(state); }
bool StateSpace::is_deadend_state(State state) const { return is_deadend_state(get_index(state)); }

bool StateSpace::is_alive_state(Index state) const { return !(get_goal_state_indices().count(state) || get_deadend_state_indices().count(state)); }
bool StateSpace::is_alive_state(State state) const { return is_alive_state(get_index(state)); }

/* Transitions */
const GroundActionEdgeList& StateSpace::get_transitions() const { return m_graph.get_edges(); }

ContinuousCost StateSpace::get_transition_cost(Index transition) const { return (m_use_unit_cost_one) ? 1 : get_cost(m_graph.get_edges().at(transition)); }

size_t StateSpace::get_num_transitions() const { return m_graph.get_num_edges(); }

/* Distances */
const ContinuousCostList& StateSpace::get_goal_distances() const { return m_goal_distances; }

ContinuousCost StateSpace::get_goal_distance(Index state) const { return m_goal_distances.at(state); }

ContinuousCost StateSpace::get_goal_distance(State state) const { return get_goal_distance(get_index(state)); }

ContinuousCost StateSpace::get_max_goal_distance() const { return *std::max_element(m_goal_distances.begin(), m_goal_distances.end()); }

/* Additional */
const std::map<ContinuousCost, IndexList>& StateSpace::get_state_indices_by_goal_distance() const { return m_states_by_goal_distance; }

/**
 * Pretty printing
 */

std::ostream& operator<<(std::ostream& out, const StateSpace& state_space)
{
    // 2. Header
    out << "digraph {"
        << "\n"
        << "rankdir=\"LR\""
        << "\n";

    // 3. Draw states
    for (Index state_index = 0; state_index < state_space.get_num_states(); ++state_index)
    {
        out << "s" << state_index << "[";
        if (state_space.is_goal_state(state_index))
        {
            out << "peripheries=2,";
        }

        // label
        out << "label=\""
            << std::make_tuple(state_space.get_problem(),
                               mimir::get_state(state_space.get_graph().get_vertices().at(state_index)),
                               std::cref(*state_space.get_pddl_repositories()))
            << "\"";

        out << "]\n";
    }

    // 4. Draw initial state and dangling edge
    out << "Dangling [ label = \"\", style = invis ]\n"
        << "{ rank = same; Dangling }\n"
        << "Dangling -> s" << state_space.get_initial_state_index() << "\n";

    // 5. Group states with same distance together
    for (auto it = state_space.get_state_indices_by_goal_distance().rbegin(); it != state_space.get_state_indices_by_goal_distance().rend(); ++it)
    {
        const auto& [goal_distance, state_indices] = *it;
        out << "{ rank = same; ";
        for (auto state_index : state_indices)
        {
            out << "s" << state_index;
            if (state_index != state_indices.back())
            {
                out << ",";
            }
        }
        out << "}\n";
    }
    // 6. Draw transitions
    for (const auto& transition : state_space.get_graph().get_edges())
    {
        // direction
        out << "s" << transition.get_source() << "->"
            << "s" << transition.get_target() << " [";

        // label
        out << "label=\"" << std::make_tuple(std::cref(*state_space.get_pddl_repositories()), get_creating_action(transition)) << "\"";

        out << "]\n";
    }
    out << "}\n";

    return out;
}
}
