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

#ifndef MIMIR_DATASETS_FAITHFUL_ABSTRACTION_HPP_
#define MIMIR_DATASETS_FAITHFUL_ABSTRACTION_HPP_

#include "mimir/common/grouped_vector.hpp"
#include "mimir/common/hash.hpp"
#include "mimir/datasets/abstract_transition.hpp"
#include "mimir/datasets/abstraction.hpp"
#include "mimir/datasets/declarations.hpp"
#include "mimir/datasets/state_space.hpp"
#include "mimir/graphs/certificate.hpp"
#include "mimir/graphs/graph_vertices.hpp"
#include "mimir/graphs/object_graph.hpp"
#include "mimir/search/applicable_action_generators.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/search/state.hpp"
#include "mimir/search/state_repository.hpp"

#include <loki/details/utils/filesystem.hpp>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace mimir
{

/// @brief `FaithfulAbstractionOptions` enscapsulates options to create a single `FaithfulAbstraction` with default arguments.
struct FaithfulAbstractionOptions
{
    bool mark_true_goal_literals = false;
    bool use_unit_cost_one = true;
    bool remove_if_unsolvable = true;
    bool compute_complete_abstraction_mapping = false;
    uint32_t max_num_concrete_states = std::numeric_limits<uint32_t>::max();
    uint32_t max_num_abstract_states = std::numeric_limits<uint32_t>::max();
    uint32_t timeout_ms = std::numeric_limits<uint32_t>::max();
    ObjectGraphPruningStrategyEnum pruning_strategy = ObjectGraphPruningStrategyEnum::None;
};

/// @brief `FaithfulAbstractionOptions` enscapsulates options to create a `FaithfulAbstractionList` with default arguments.
struct FaithfulAbstractionsOptions
{
    FaithfulAbstractionOptions fa_options;
    bool sort_ascending_by_num_states = true;
    uint32_t num_threads = std::thread::hardware_concurrency();
};

/// @brief `FaithfulAbstractState` encapsulates data of an abstract state in a `FaithfulAbstraction`.
struct FaithfulAbstractStateTag
{
};

using FaithfulAbstractState = Vertex<FaithfulAbstractStateTag, std::span<const State>, std::shared_ptr<const Certificate>>;
using FaithfulAbstractStateList = std::vector<FaithfulAbstractState>;

inline std::span<const State> get_states(const FaithfulAbstractState& state) { return state.get_property<0>(); }

inline State get_representative_state(const FaithfulAbstractState& state)
{
    assert(!state.get_property<0>().empty());
    return state.get_property<0>().front();
}

inline const std::shared_ptr<const Certificate>& get_certificate(const FaithfulAbstractState& state) { return state.get_property<1>(); }

/// @brief `FaithfulAbstraction` implements abstractions based on isomorphism testing.
/// Source: https://mrlab.ai/papers/drexler-et-al-icaps2024wsprl.pdf
///
/// The underlying graph type is a `StaticBidirectionalGraph` over `FaithfulAbstractState` and `AbstractTransition`.
/// The `FaithfulAbstraction` stores additional external properties on vertices such as initial state, goal states, deadend states.
/// The getters are simple adapters to follow the notion of states and transitions from the literature.
///
/// Graph algorithms should be applied on the underlying graph, see e.g., `compute_pairwise_shortest_state_distances()`.
class FaithfulAbstraction
{
public:
    using GraphType = StaticBidirectionalGraph<StaticGraph<FaithfulAbstractState, AbstractTransition>>;

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
    /// @brief Constructs a `FaithfulAbstraction` from the given data.
    /// The create function calls this constructor and ensures that
    /// the `FaithfulAbstraction` is in a legal state allowing other parts of
    /// the code base to operate on the invariants in the implementation.
    FaithfulAbstraction(Problem problem,
                        bool mark_true_goal_literals,
                        bool use_unit_cost_one,
                        std::shared_ptr<PDDLFactories> factories,
                        std::shared_ptr<IApplicableActionGenerator> aag,
                        std::shared_ptr<StateRepository> ssg,
                        GraphType graph,
                        std::shared_ptr<const StateList> concrete_states_by_abstract_state,
                        StateMap<StateIndex> concrete_to_abstract_state,
                        StateIndex initial_state,
                        StateIndexSet goal_states,
                        StateIndexSet deadend_states,
                        std::shared_ptr<const GroundActionList> ground_actions_by_source_and_target,
                        DistanceList goal_distances);

public:
    static std::optional<FaithfulAbstraction>
    create(const fs::path& domain_filepath, const fs::path& problem_filepath, const FaithfulAbstractionOptions& options = FaithfulAbstractionOptions());

    /// @brief Try to create a `FaithfulAbstraction` from the given input files with the given options.
    /// @param problem is the problem.
    /// @param factories is the external PDDL factories.
    /// @param aag is the external applicable action generator.
    /// @param ssg is the external successor state generator.
    /// @param options the options.
    /// @return std::nullopt if discarded, or otherwise, a FaithfulAbstraction.
    static std::optional<FaithfulAbstraction> create(Problem problem,
                                                     std::shared_ptr<PDDLFactories> factories,
                                                     std::shared_ptr<IApplicableActionGenerator> aag,
                                                     std::shared_ptr<StateRepository> ssg,
                                                     const FaithfulAbstractionOptions& options = FaithfulAbstractionOptions());

    /// @brief Convenience function when sharing parsers, aags, ssgs is not relevant.
    static std::vector<FaithfulAbstraction> create(const fs::path& domain_filepath,
                                                   const std::vector<fs::path>& problem_filepaths,
                                                   const FaithfulAbstractionsOptions& options = FaithfulAbstractionsOptions());

    /// @brief Try to create a FaithfulAbstractionList from the given data and the given options.
    /// @param memories External memory to problem, factories, aags, ssgs.
    /// @param options the options.
    /// @return `FaithfulAbstractionList` contains the `FaithfulAbstraction`s for which the construction was successful.
    static std::vector<FaithfulAbstraction> create(
        const std::vector<std::tuple<Problem, std::shared_ptr<PDDLFactories>, std::shared_ptr<IApplicableActionGenerator>, std::shared_ptr<StateRepository>>>&
            memories,
        const FaithfulAbstractionsOptions& options = FaithfulAbstractionsOptions());

    /**
     * Abstraction functionality
     */

    StateIndex get_abstract_state_index(State concrete_state) const;

    /**
     * Extended functionality
     */

    /// @brief Compute the shortest distances from the given states using Breadth-first search (unit cost 1) or Dijkstra.
    /// @tparam Direction the direction of traversal.
    /// @param states the list of states from which shortest distances are computed.
    /// @return the shortest distances from the given states to all other states.
    template<IsTraversalDirection Direction>
    DistanceList compute_shortest_distances_from_states(const StateIndexList& states) const;

    /// @brief Compute pairwise shortest distances using Floyd-Warshall.
    /// @tparam Direction the direction of traversal.
    /// @return the pairwise shortest distances.
    template<IsTraversalDirection Direction>
    DistanceMatrix compute_pairwise_shortest_state_distances() const;

    /**
     * Getters.
     */

    /* Meta data */
    Problem get_problem() const;
    bool get_mark_true_goal_literals() const;
    bool get_use_unit_cost_one() const;

    /* Memory */
    const std::shared_ptr<PDDLFactories>& get_pddl_factories() const;
    const std::shared_ptr<IApplicableActionGenerator>& get_aag() const;
    const std::shared_ptr<StateRepository>& get_ssg() const;

    /* Graph */
    const GraphType& get_graph() const;

    /* States */
    const FaithfulAbstractStateList& get_states() const;
    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentVertexConstIteratorType<Direction>> get_adjacent_states(StateIndex state) const;
    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentVertexIndexConstIteratorType<Direction>> get_adjacent_state_indices(StateIndex state) const;
    const StateMap<StateIndex>& get_concrete_to_abstract_state() const;
    StateIndex get_initial_state() const;
    const StateIndexSet& get_goal_states() const;
    const StateIndexSet& get_deadend_states() const;
    size_t get_num_states() const;
    size_t get_num_goal_states() const;
    size_t get_num_deadend_states() const;
    bool is_goal_state(StateIndex state) const;
    bool is_deadend_state(StateIndex state) const;
    bool is_alive_state(StateIndex state) const;

    /* Transitions */
    const AbstractTransitionList& get_transitions() const;
    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentEdgeConstIteratorType<Direction>> get_adjacent_transitions(StateIndex state) const;
    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentEdgeIndexConstIteratorType<Direction>> get_adjacent_transition_indices(StateIndex state) const;
    TransitionCost get_transition_cost(TransitionIndex transition) const;
    size_t get_num_transitions() const;

    /* Distances */
    const DistanceList& get_goal_distances() const;
    Distance get_goal_distance(StateIndex state) const;

    /* Additional */
    const std::map<Distance, StateIndexList>& get_states_by_goal_distance() const;

private:
    /* Meta data */
    Problem m_problem;
    bool m_mark_true_goal_literals;
    bool m_use_unit_cost_one;

    /* Memory */
    std::shared_ptr<PDDLFactories> m_pddl_factories;
    std::shared_ptr<IApplicableActionGenerator> m_aag;
    std::shared_ptr<StateRepository> m_ssg;

    /* States */
    GraphType m_graph;
    // Persistent and sorted to store slices in the abstract states.
    std::shared_ptr<const StateList> m_concrete_states_by_abstract_state;
    StateMap<StateIndex> m_concrete_to_abstract_state;
    StateIndex m_initial_state;
    StateIndexSet m_goal_states;
    StateIndexSet m_deadend_states;

    /* Transitions */
    // Persistent and sorted to store slices in the abstract transitions.
    std::shared_ptr<const GroundActionList> m_ground_actions_by_source_and_target;

    /* Distances */
    DistanceList m_goal_distances;

    /* Additional */
    std::map<Distance, StateIndexList> m_states_by_goal_distance;
};

static_assert(IsAbstraction<FaithfulAbstraction>);

using FaithfulAbstractionList = std::vector<FaithfulAbstraction>;

/**
 * Static assertions
 */

// static_assert(IsAbstraction<FaithfulAbstraction>);

/**
 * Pretty printing
 */

extern std::ostream& operator<<(std::ostream& out, const FaithfulAbstraction& abstraction);

}

#endif
