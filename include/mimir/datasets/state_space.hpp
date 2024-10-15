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

#ifndef MIMIR_DATASETS_STATE_SPACE_HPP_
#define MIMIR_DATASETS_STATE_SPACE_HPP_

#include "mimir/common/grouped_vector.hpp"
#include "mimir/datasets/ground_action_edge.hpp"
#include "mimir/datasets/state_vertex.hpp"
#include "mimir/formalism/factories.hpp"
#include "mimir/formalism/parser.hpp"
#include "mimir/graphs/static_graph.hpp"
#include "mimir/search/action.hpp"
#include "mimir/search/applicable_action_generators.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/search/state.hpp"
#include "mimir/search/state_repository.hpp"

#include <cstddef>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mimir
{

/// @brief `StateSpaceOptions` encapsulates options to create a single state space with default parameters.
struct StateSpaceOptions
{
    bool use_unit_cost_one = true;
    bool remove_if_unsolvable = true;
    uint32_t max_num_states = std::numeric_limits<uint32_t>::max();
    uint32_t timeout_ms = std::numeric_limits<uint32_t>::max();
};

/// @brief `StateSpacesOptions` encapsulates options to create a collection of state spaces with default parameters.
struct StateSpacesOptions
{
    StateSpaceOptions state_space_options = StateSpaceOptions();
    bool sort_ascending_by_num_states = true;
    uint32_t num_threads = std::thread::hardware_concurrency();
};

/// @brief `StateSpace` encapsulates the complete dynamics of a PDDL problem.
///
/// The underlying graph type is a `StaticBidirectionalGraph` over `StateVertex` and `GroundActionEdge`.
/// The `StateSpace` stores additional external properties on vertices such as initial state, goal states, deadend states.
/// The getters are simple adapters to follow the notion of states and transitions from the literature.
///
/// Graph algorithms should be applied on the underlying graph, see e.g., `compute_pairwise_shortest_state_distances()`.
class StateSpace
{
public:
    using GraphType = StaticBidirectionalGraph<StaticGraph<StateVertex, GroundActionEdge>>;

    using VertexIndexConstIteratorType = typename GraphType::VertexIndexConstIteratorType;
    using EdgeIndexConstIteratorType = typename GraphType::EdgeIndexConstIteratorType;

    template<IsTraversalDirection Direction>
    using AdjacentVertexConstIteratorType = typename GraphType::AdjacentVertexConstIteratorType<Direction>;
    template<IsTraversalDirection Direction>
    using AdjacentVertexIndexConstIteratorType = typename GraphType::AdjacentVertexIndexConstIteratorType<Direction>;
    template<IsTraversalDirection Direction>
    using AdjacentEdgeConstIteratorType = typename GraphType::AdjacentEdgeConstIteratorType<Direction>;
    template<IsTraversalDirection Direction>
    using AdjacentEdgeIndexConstIteratorType = typename GraphType::AdjacentEdgeIndexConstIteratorType<Direction>;

private:
    /// @brief Constructs a `StateSpace` from data.
    /// The create function calls this constructor and ensures that
    /// the `StateSpace` is in a legal state allowing other parts of
    /// the code base to operate on the invariants in the implementation.
    StateSpace(Problem problem,
               bool use_unit_cost_one,
               std::shared_ptr<PDDLFactories> pddl_factories,
               std::shared_ptr<IApplicableActionGenerator> aag,
               std::shared_ptr<StateRepository> ssg,
               GraphType graph,
               StateMap<Index> state_to_index,
               Index initial_state,
               IndexSet goal_states,
               IndexSet deadend_states,
               ContinuousCostList goal_distances);

public:
    /// @brief Try to create a `StateSpace` from the given input files with the given options.
    /// @param problem The problem from which to create the state space.
    /// @param parser External memory to PDDLFactories.
    /// @param aag External memory to aag.
    /// @param ssg External memory to ssg.
    /// @param options the options.
    /// @return StateSpace if construction is within the given options, and otherwise nullptr.
    static std::optional<StateSpace> create(Problem problem,
                                            std::shared_ptr<PDDLFactories> factories,
                                            std::shared_ptr<IApplicableActionGenerator> aag,
                                            std::shared_ptr<StateRepository> ssg,
                                            const StateSpaceOptions& options = StateSpaceOptions());

    /// @brief Convenience function when sharing parsers, aags, ssgs is not relevant.
    static std::optional<StateSpace>
    create(const fs::path& domain_filepath, const fs::path& problem_filepath, const StateSpaceOptions& options = StateSpaceOptions());

    /// @brief Convenience function when sharing parsers, aags, ssgs is not relevant.
    static std::vector<StateSpace>
    create(const fs::path& domain_filepath, const std::vector<fs::path>& problem_filepaths, const StateSpacesOptions& options = StateSpacesOptions());

    /// @brief Try to create a `StateSpaceList` from the given data and the given options.
    /// @param memories External memory to problems, parsers, aags, ssgs.
    /// @param options the options.
    /// @return `StateSpaceList` contains the `StateSpace`s for which the construction was successful.
    static std::vector<StateSpace> create(
        const std::vector<std::tuple<Problem, std::shared_ptr<PDDLFactories>, std::shared_ptr<IApplicableActionGenerator>, std::shared_ptr<StateRepository>>>&
            memories,
        const StateSpacesOptions& options = StateSpacesOptions());

    /**
     * Extended functionality
     */

    /// @brief Compute the shortest distances from the given states using Breadth-first search (unit cost 1) or Dijkstra.
    /// @tparam Direction the direction of traversal.
    /// @param states the list of states from which shortest distances are computed.
    /// @return the shortest distances from the given states to all other states.
    template<IsTraversalDirection Direction>
    ContinuousCostList compute_shortest_distances_from_states(const IndexList& states) const;

    /// @brief Compute pairwise shortest distances using Floyd-Warshall.
    /// @tparam Direction the direction of traversal.
    /// @return the pairwise shortest distances.
    template<IsTraversalDirection Direction>
    ContinuousCostMatrix compute_pairwise_shortest_state_distances() const;

    /**
     *  Getters
     */

    /* Meta data */
    Problem get_problem() const;
    bool get_use_unit_cost_one() const;

    /* Memory */
    const std::shared_ptr<PDDLFactories>& get_pddl_factories() const;
    const std::shared_ptr<IApplicableActionGenerator>& get_aag() const;
    const std::shared_ptr<StateRepository>& get_ssg() const;

    /* Graph */
    const GraphType& get_graph() const;

    /* States */
    template<IsTraversalDirection Direction>
    auto get_adjacent_state_indices(Index state) const
    {
        return m_graph.get_adjacent_vertex_indices<Direction>(state);
    }
    template<IsTraversalDirection Direction>
    auto get_adjacent_states(State state) const
    {
        return m_graph.get_adjacent_vertex_indices<Direction>(get_index(state))
               | std::views::transform([&](Index vertex_index) { return mimir::get_state(get_graph().get_vertex(vertex_index)); });
    }
    Index get_index(State state) const;
    State get_state(Index state) const;
    Index get_initial_state_index() const;
    State get_initial_state() const;
    size_t get_num_deadend_states() const;
    bool is_goal_state(State state) const;
    bool is_goal_state(Index state) const;
    bool is_deadend_state(State state) const;
    bool is_deadend_state(Index state) const;
    bool is_alive_state(State state) const;
    bool is_alive_state(Index state) const;
    const IndexSet& get_goal_state_indices() const;
    const IndexSet& get_deadend_state_indices() const;
    size_t get_num_states() const;
    size_t get_num_goal_states() const;

    /* Transitions */
    const GroundActionEdgeList& get_transitions() const;
    template<IsTraversalDirection Direction, typename StateT>
        requires std::same_as<StateT, State> or std::same_as<StateT, Index>
    auto get_adjacent_transitions(StateT state) const;
    ContinuousCost get_transition_cost(Index transition) const;
    size_t get_num_transitions() const;

    /* Distances */
    const ContinuousCostList& get_goal_distances() const;
    ContinuousCost get_goal_distance(Index state) const;
    ContinuousCost get_goal_distance(State state) const;
    ContinuousCost get_max_goal_distance() const;

    /* Additional */
    const std::map<ContinuousCost, IndexList>& get_state_indices_by_goal_distance() const;
    auto get_states_view_by_goal_distance(ContinuousCost goal_distance) const
    {
        return get_state_indices_by_goal_distance().at(goal_distance) | std::views::transform(AS_CPTR_LAMBDA(get_state));
    }
    template<typename RNGType>
    Index sample_state_index_with_goal_distance(ContinuousCost goal_distance, RNGType& rng) const {
            const auto& states = m_states_by_goal_distance.at(goal_distance);
            const auto index = std::uniform_int_distribution<size_t> { 0ul, states.size() }(rng);
            return states.at(index);
    }
    template<typename RNGType>
    State sample_state_with_goal_distance(ContinuousCost goal_distance, RNGType& rng ) const {
        return get_state(sample_state_index_with_goal_distance(goal_distance, rng));
    }

private:
    /* Meta data */
    Problem m_problem;
    bool m_use_unit_cost_one;

    /* Memory */
    std::shared_ptr<PDDLFactories> m_pddl_factories;
    std::shared_ptr<IApplicableActionGenerator> m_aag;
    std::shared_ptr<StateRepository> m_ssg;

    /* States */
    GraphType m_graph;
    StateMap<Index> m_state_to_index;
    Index m_initial_state;
    IndexSet m_goal_states;
    IndexSet m_deadend_states;

    /* Distances */
    ContinuousCostList m_goal_distances;

    /* Additional */
    std::map<ContinuousCost, IndexList> m_states_by_goal_distance;
};

using StateSpaceList = std::vector<StateSpace>;

/**
 * Pretty printing
 */

extern std::ostream& operator<<(std::ostream& out, const StateSpace& state_space);

/**
 * Implementation
 */

template<IsTraversalDirection Direction, typename StateT>
    requires std::same_as<StateT, State> or std::same_as<StateT, Index>
auto StateSpace::get_adjacent_transitions(StateT state) const
{
    return m_graph.get_adjacent_edges<Direction>(std::invoke(overload { [&](State state) { return get_index(state); }, std::identity {} }, state))
           | std::views::transform(
               [&](const GraphType::EdgeType& edge)
               {
                   if constexpr (std::same_as<StateT, State>)
                   {
                       return StateEdgeType { edge.get_index(), get_state(edge.get_source()), get_state(edge.get_target()) };
                   }
                   else
                   {
                       return edge;
                   }
               });
}
}

#endif
