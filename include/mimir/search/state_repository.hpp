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

#ifndef MIMIR_SEARCH_STATE_REPOSITORY_HPP_
#define MIMIR_SEARCH_STATE_REPOSITORY_HPP_

#include "mimir/algorithms/shared_object_pool.hpp"
#include "mimir/common/types_cista.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/search/state.hpp"
#include "mimir/search/state_unpacked.hpp"

#include <absl/container/flat_hash_map.h>
#include <chrono>

namespace mimir::search
{

class StateRepositoryImpl : public std::enable_shared_from_this<StateRepositoryImpl>
{
public:
    struct IndexListHash
    {
        size_t operator()(const IndexList& list) const
        {
            auto seed = list.size();
            for (const auto index : list)
            {
                loki::hash_combine(seed, index);
            }
            return seed;
        }
    };

    struct StagedSuccessorState
    {
        FlatBitset fluent_atoms;
        FlatBitset derived_atoms;
        IndexList fluent_atom_indices;
        IndexList derived_atom_indices;
        FlatDoubleList fluent_numeric_variables;
        ContinuousCost metric_value;
    };

    struct StagedSuccessorHandle
    {
        Index state_index;
        PackedState packed_state;
    };

    struct StagedSuccessorScratch
    {
        FlatBitset applied_positive_effect_atoms;
        FlatBitset applied_negative_effect_atoms;
        SharedObjectPool<UnpackedStateImpl> unpacked_state_pool;
    };

    struct StagedSuccessorInternTimings
    {
        std::chrono::nanoseconds fluent_slot_time = std::chrono::nanoseconds::zero();
        std::chrono::nanoseconds numeric_slot_time = std::chrono::nanoseconds::zero();
        std::chrono::nanoseconds derived_slot_time = std::chrono::nanoseconds::zero();
        std::chrono::nanoseconds state_lookup_time = std::chrono::nanoseconds::zero();
        std::chrono::nanoseconds reached_atom_update_time = std::chrono::nanoseconds::zero();
    };

private:
    AxiomEvaluator m_axiom_evaluator;  ///< The axiom evaluator.

    PackedStateImplMap m_states;  ///< Stores all created extended states.
    absl::flat_hash_map<IndexList, valla::Slot<Index>, IndexListHash> m_fluent_atom_slots;  ///< Memoizes fluent atom sequences to tree slots.

    FlatBitset m_reached_fluent_atoms;   ///< Stores all encountered fluent atoms.
    FlatBitset m_reached_derived_atoms;  ///< Stores all encountered derived atoms.

    /* Memory for reuse */

    FlatBitset m_applied_positive_effect_atoms;
    FlatBitset m_applied_negative_effect_atoms;

    IndexList m_index_list;

    SharedObjectPool<UnpackedStateImpl> m_unpacked_state_pool;

public:
    explicit StateRepositoryImpl(AxiomEvaluator axiom_evaluator);

    static StateRepository create(AxiomEvaluator axiom_evaluator);

    StateRepositoryImpl(const StateRepositoryImpl& other) = delete;
    StateRepositoryImpl& operator=(const StateRepositoryImpl& other) = delete;
    StateRepositoryImpl(StateRepositoryImpl&& other) = delete;
    StateRepositoryImpl& operator=(StateRepositoryImpl&& other) = delete;

    /// @brief Get or create the initial state of the underlying problem.
    /// @return the initial state and its associated metric value, which is 0 in the case of :action-costs.
    std::pair<State, ContinuousCost> get_or_create_initial_state();

    /// @brief Get or create the state for a given set of ground `atoms`.
    /// @param atoms the ground atoms.
    /// @param fluent_numeric_variables are the numeric variables in the state.
    /// @return the state and its associated metric value, which is 0 in the case of :action-costs.
    std::pair<State, ContinuousCost> get_or_create_state(const formalism::GroundAtomList<formalism::FluentTag>& atoms,
                                                         const FlatDoubleList& fluent_numeric_variables);

    /// @brief Get or create the successor state when applying the given ground `action` in the given `state`.
    /// @param state is the state.
    /// @param action is the ground action.
    /// @param state_metric_value is the metric value of the state.
    /// @return the successor state and its associated metric value.
    std::pair<State, ContinuousCost> get_or_create_successor_state(const State& state, formalism::GroundAction action, ContinuousCost state_metric_value);

    /// @brief Compute a successor into worker-local dense storage for the grounded
    /// parallel beam path. This does not mutate the repository state.
    StagedSuccessorState compute_staged_successor_state(const State& state,
                                                        formalism::GroundAction action,
                                                        ContinuousCost state_metric_value,
                                                        StagedSuccessorScratch& scratch) const;

    /// @brief Materialize a worker-computed parallel beam successor into the
    /// canonical repository state map on the main search thread.
    StagedSuccessorHandle get_or_create_staged_successor_handle(const StagedSuccessorState& successor_state,
                                                                StagedSuccessorInternTimings* timings = nullptr);

    /// @brief Materialize a canonical repository state from a staged successor handle.
    State materialize_staged_successor_state(const StagedSuccessorState& successor_state, const StagedSuccessorHandle& successor_handle);

    /// @brief Build a temporary successor state for worker-side novelty checks and
    /// eager beam scoring. The returned state is not inserted into the repository.
    State make_temporary_staged_successor_state(const StagedSuccessorState& successor_state,
                                                const StagedSuccessorHandle& successor_handle,
                                                StagedSuccessorScratch& scratch);

    /// @brief Get the state with the given packed state.
    /// This operation unpacks the state.
    /// @param state is the packed state.
    /// @return the state.
    State get_state(const PackedStateImpl& state);

    /// @brief Get the index of a given packed state.
    /// This operation has constant time.
    /// @param state is the packed state.
    /// @return the index.
    Index get_state_index(const PackedStateImpl& state);

    /**
     * Getters
     */

    const formalism::Problem& get_problem() const;

    /// @brief Return the number of created states.
    /// @return the number of created states.
    size_t get_state_count() const;

    /// @brief Return the state map.
    /// @return the states map.
    const PackedStateImplMap& get_states() const;

    /// @brief Return the reached fluent ground atoms.
    /// @return a bitset that stores the reached fluent ground atom indices.
    const FlatBitset& get_reached_fluent_ground_atoms_bitset() const;

    /// @brief Return the reached derived ground atoms.
    /// @return a bitset that stores the reached derived ground atom indices.
    const FlatBitset& get_reached_derived_ground_atoms_bitset() const;

    /// @brief Get the underlying axiom evaluator.
    /// @return the axiom evaluator.
    const AxiomEvaluator& get_axiom_evaluator() const;
};

/**
 * Utils
 */

extern ContinuousCost compute_state_metric_value(const State& state);

}

#endif
