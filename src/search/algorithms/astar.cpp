/*
 * Copyright (C) 2023 Dominik Drexler and Simon Stahlberg
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

#include "mimir/search/algorithms/astar.hpp"

#include "mimir/search/algorithms/astar/event_handlers.hpp"
#include "mimir/search/algorithms/strategies/goal_strategy.hpp"
#include "mimir/search/algorithms/strategies/pruning_strategy.hpp"
#include "mimir/search/openlists/interface.hpp"
#include "mimir/search/successor_state_generator.hpp"

namespace mimir
{

AStarAlgorithm::AStarAlgorithm(std::shared_ptr<IApplicableActionGenerator> applicable_action_generator, std::shared_ptr<IHeuristic> heuristic) :
    AStarAlgorithm(applicable_action_generator, std::make_shared<SuccessorStateGenerator>(applicable_action_generator), std::move(heuristic))
{
}

AStarAlgorithm::AStarAlgorithm(std::shared_ptr<IApplicableActionGenerator> applicable_action_generator,
                               std::shared_ptr<SuccessorStateGenerator> successor_state_generator,
                               std::shared_ptr<IHeuristic> heuristic) :
    m_aag(std::move(applicable_action_generator)),
    m_ssg(std::move(successor_state_generator)),
    m_initial_state(m_ssg->get_or_create_initial_state()),
    m_heuristic(std::move(heuristic))
{
}

SearchStatus AStarAlgorithm::find_solution(std::vector<GroundAction>& out_plan) { return find_solution(m_initial_state, out_plan); }

SearchStatus AStarAlgorithm::find_solution(State start_state, std::vector<GroundAction>& out_plan)
{
    std::optional<State> unused_out_state = std::nullopt;
    return find_solution(start_state, out_plan, unused_out_state);
}

SearchStatus AStarAlgorithm::find_solution(State start_state, std::vector<GroundAction>& out_plan, std::optional<State>& out_goal_state)
{
    return find_solution(start_state, std::make_unique<ProblemGoal>(m_aag->get_problem()), std::make_unique<DuplicateStatePruning>(), out_plan, out_goal_state);
}

SearchStatus AStarAlgorithm::find_solution(State start_state,
                                           std::unique_ptr<IGoalStrategy>&& goal_strategy,
                                           std::unique_ptr<IPruningStrategy>&& pruning_strategy,
                                           std::vector<GroundAction>& out_plan,
                                           std::optional<State>& out_goal_state)
{
    /*
    // Clear data structures
    m_search_nodes.clear();
    m_openlist->clear();

    const auto problem = m_aag->get_problem();
    const auto& pddl_factories = *m_aag->get_pddl_factories();
    m_event_handler->on_start_search(problem, start_state, pddl_factories);

    auto initial_search_node = InformedSearchNode(this->m_search_nodes[start_state.get_id()]);
    initial_search_node.get_g_value() = 0;
    initial_search_node.get_status() = SearchNodeStatus::OPEN;

    if (!goal_strategy->test_static_goal())
    {
        m_event_handler->on_unsolvable();

        return SearchStatus::UNSOLVABLE;
    }

    auto applicable_actions = GroundActionList {};

    if (pruning_strategy->test_prune_initial_state(start_state))
    {
        return SearchStatus::FAILED;
    }

    m_queue.emplace_back(start_state);

    auto g_value = uint64_t { 0 };

    while (!m_queue.empty())
    {
        const auto state = m_queue.front();
        m_queue.pop_front();

        // We need this before goal test for correct statistics reporting.
        auto search_node = InformedSearchNode(this->m_search_nodes[state.get_id()]);
        search_node.get_status() = SearchNodeStatus::CLOSED;

        if (static_cast<uint64_t>(search_node.get_g_value()) > g_value)
        {
            g_value = search_node.get_g_value();
            m_aag->on_finish_f_layer();
            m_event_handler->on_finish_f_layer();
        }

        if (goal_strategy->test_dynamic_goal(state))
        {
            set_plan(ConstInformedSearchNode(this->m_search_nodes[state.get_id()]), out_plan);
            out_goal_state = state;
            m_event_handler->on_end_search();
            if (!m_event_handler->is_quiet())
            {
                m_aag->on_end_search();
            }
            m_event_handler->on_solved(out_plan);

            return SearchStatus::SOLVED;
        }

        m_event_handler->on_expand_state(problem, state, pddl_factories);

        this->m_aag->generate_applicable_actions(state, applicable_actions);

        for (const auto& action : applicable_actions)
        {
            const auto state_count = m_ssg->get_state_count();
            const auto& successor_state = this->m_ssg->get_or_create_successor_state(state, action);

            m_event_handler->on_generate_state(problem, action, successor_state, pddl_factories);

            bool is_new_successor_state = (state_count != m_ssg->get_state_count());

            if (!pruning_strategy->test_prune_successor_state(state, successor_state, is_new_successor_state))
            {
                auto successor_search_node = InformedSearchNode(this->m_search_nodes[successor_state.get_id()]);
                successor_search_node.get_status() = SearchNodeStatus::OPEN;
                successor_search_node.get_g_value() = search_node.get_g_value() + 1;
                successor_search_node.get_parent_state_id() = state.get_id();
                successor_search_node.get_creating_action_id() = action.get_id();

                m_queue.emplace_back(successor_state);
            }
            else
            {
                m_event_handler->on_prune_state(problem, successor_state, pddl_factories);
            }
        }
    }

    m_event_handler->on_end_search();
    m_event_handler->on_exhausted();

    */
    return SearchStatus::EXHAUSTED;
}

}