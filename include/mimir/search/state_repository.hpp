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
#include <valla/indexed_hash_set.hpp>
#include <valla/slot.hpp>

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
        ParallelAxiomWorkerContext axiom_worker_context;

        StagedSuccessorScratch();
        ~StagedSuccessorScratch();
    };

    /// @brief Tag requesting that the repository own its `valla` interning tables instead
    /// of sharing the `Problem`'s. A `valla::Slot` is meaningful only relative to the table
    /// that produced it, and every decode path already lives inside this class, so private
    /// tables change no existing invariant -- they only stop independent searches from
    /// sharing a namespace they never needed to share.
    ///
    /// This is what lets K independent rollouts run over one shared `Problem` with real
    /// parallelism: the shared tables are the only contended structure during a grounded
    /// search, and they are lock-striped, so sharing them makes threading strictly harmful.
    /// See docs/PARALLEL_IW_ROLLOUTS.md.
    ///
    /// ATTENTION: a `State` created by a repository with private tables is not a valid
    /// lookup key in any other repository. Callers must re-create the start state through
    /// this repository (see `get_or_create_state`).
    ///
    /// ATTENTION: private tables do NOT make a repository independent of every other one
    /// over the same `Problem`. Two things stay shared and mutable:
    ///   * `m_axiom_evaluator` -- `find_rollouts_parallel` hands the SAME evaluator to all
    ///     K repositories and to the caller's. It is safe today only because
    ///     `generate_and_apply_axioms` keeps its scratch function-local and writes solely
    ///     into the caller-supplied `UnpackedStateImpl`; that is a property to preserve,
    ///     not an accident to rely on silently.
    ///   * the `m_event_handler` inside the shared applicable-action generator and axiom
    ///     evaluator, which every rollout writes to (statistics only, but genuinely raced).
    /// Separately, `m_unpacked_state_pool` and the `SharedObjectPoolPtr` refcount it hands
    /// out are unsynchronized, so a `State` belonging to THIS repository must never be
    /// copied or destroyed on another thread.
    struct PrivateInterningTables
    {
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
    using IndexTreeTable = valla::IndexedHashSet<valla::Slot<Index>, Index>;
    using DoubleLeafTable = valla::IndexedHashSet<double, Index>;

    struct OwnedInterningTables
    {
        IndexTreeTable index_tree;
        DoubleLeafTable double_leaf;
    };

    AxiomEvaluator m_axiom_evaluator;  ///< The axiom evaluator.

    std::unique_ptr<OwnedInterningTables> m_owned_interning_tables;  ///< Null when sharing the `Problem`'s tables.
    IndexTreeTable& m_index_tree_table;    ///< Bound once at construction, never rebound.
    DoubleLeafTable& m_double_leaf_table;  ///< Bound once at construction, never rebound.

    PackedStateImplMap m_states;  ///< Stores all created extended states.
    std::vector<PackedState> m_packed_states_by_index;
    absl::flat_hash_map<IndexList, valla::Slot<Index>, IndexListHash> m_fluent_atom_slots;  ///< Memoizes fluent atom sequences to tree slots.

    FlatBitset m_reached_fluent_atoms;   ///< Stores all encountered fluent atoms.
    FlatBitset m_reached_derived_atoms;  ///< Stores all encountered derived atoms.

    bool m_track_first_achievers;                 ///< Opt-in; see `enable_first_achiever_tracking`.
    std::vector<Index> m_first_achiever_by_atom;  ///< Fluent atom index -> state index, or `MAX_INDEX`.

    bool m_track_co_occurrence;                       ///< Opt-in; see `enable_co_occurrence_tracking`.
    std::vector<FlatBitset> m_co_occurrence_by_atom;  ///< Fluent atom index -> atoms ever true alongside it.

    /* Memory for reuse */

    FlatBitset m_applied_positive_effect_atoms;
    FlatBitset m_applied_negative_effect_atoms;
    FlatBitset m_dense_fluent_atoms_scratch;  ///< Only for the `GroundAtomList` -> dense translation.

    IndexList m_index_list;

    SharedObjectPool<UnpackedStateImpl> m_unpacked_state_pool;

private:
    /// @brief Record the first achiever of every atom `state_fluent_atoms` newly reaches.
    /// No-op unless `enable_first_achiever_tracking` was called. Must be invoked at each
    /// `update_reached_fluent_atoms` site, BEFORE the union and before the emplace.
    void record_first_achievers(const FlatBitset& state_fluent_atoms);

    /// @brief Union `state_fluent_atoms` into the co-occurrence row of each atom it holds.
    /// No-op unless `enable_co_occurrence_tracking` was called. Invoked at the same sites as
    /// `record_first_achievers`; unlike that one it is idempotent, so a site that may re-offer
    /// an already-created state costs a repeated union and nothing else.
    void record_co_occurrence(const FlatBitset& state_fluent_atoms);

public:
    /// @brief Construct a repository that interns states into the `Problem`'s shared tables.
    explicit StateRepositoryImpl(AxiomEvaluator axiom_evaluator);

    /// @brief Construct a repository that owns its interning tables, so it shares no mutable
    /// state with any other repository over the same `Problem`. See `PrivateInterningTables`.
    StateRepositoryImpl(AxiomEvaluator axiom_evaluator, PrivateInterningTables);

    static StateRepository create(AxiomEvaluator axiom_evaluator);

    static StateRepository create(AxiomEvaluator axiom_evaluator, PrivateInterningTables);

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

    /// @brief Get or create the state described by a dense set of fluent ground atom INDICES.
    ///
    /// Identical in effect to the `GroundAtomList` overload -- that one only ever uses its
    /// argument to set these same bits -- but skips materializing the atom list. This is the
    /// overload to use when moving a state BETWEEN repositories over the same `Problem`: a
    /// `PackedState`/`valla::Slot` is meaningful only relative to the interning table that
    /// produced it (see `PrivateInterningTables`), whereas ground-atom indices are stable
    /// across every repository over one grounded `Problem`, whose `Repositories` is frozen.
    /// The dense bitset is therefore the portable description of a state, and this is the
    /// entry point that re-interns it here.
    ///
    /// ATTENTION: this INSERTS. Against a repository that already holds the state -- e.g. one
    /// backing a complete `StateSpace` -- it is a pure lookup and the state count does not
    /// grow; against any other it grows the repository permanently.
    std::pair<State, ContinuousCost> get_or_create_state(const FlatBitset& fluent_atoms, const FlatDoubleList& fluent_numeric_variables);

    /// @brief Get or create the successor state when applying the given ground `action` in the given `state`.
    /// @param state is the state.
    /// @param action is the ground action.
    /// @param state_metric_value is the metric value of the state.
    /// @return the successor state and its associated metric value.
    std::pair<State, ContinuousCost> get_or_create_successor_state(const State& state, formalism::GroundAction action, ContinuousCost state_metric_value);

    /// @brief Collect fluent atoms that are added by currently triggered conditional
    /// effects of `action` in `state`, excluding atoms already true in `state`.
    void collect_action_add_effect_fluent_atom_indices(const State& state,
                                                       formalism::GroundAction action,
                                                       iw::AtomIndexList& out_add_fluent_atom_indices);

    /// @brief Collect fluent atoms that actually change truth value when applying
    /// `action` in `state`.
    void collect_action_change_effect_fluent_atom_indices(const State& state,
                                                          formalism::GroundAction action,
                                                          iw::AtomIndexList& out_add_fluent_atom_indices,
                                                          iw::AtomIndexList& out_del_fluent_atom_indices);

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

    /// @brief Start recording, per fluent ground atom, the index of the FIRST state created
    /// here in which that atom holds.
    ///
    /// Off by default because it costs a membership test per set bit of every created state,
    /// on top of the union that `m_reached_fluent_atoms` already performs. Enable it only on
    /// repositories whose achievers you actually intend to read -- e.g. an IW rollout's own
    /// repository, where the recorded state is the one a worker searching for that atom as a
    /// subgoal would land in.
    ///
    /// Must be called before any state is created; it is not retroactive.
    void enable_first_achiever_tracking();

    /// @brief Fluent atom index -> index of the first state here achieving it, or
    /// `MAX_INDEX`. Empty unless `enable_first_achiever_tracking` was called.
    const std::vector<Index>& get_first_achiever_state_by_atom() const;

    /// @brief Start recording, per fluent ground atom, every atom that ever holds in the same
    /// created state as it -- the width-2 counterpart of `m_reached_fluent_atoms`.
    ///
    /// `get_reached_fluent_ground_atoms_bitset` answers "which atoms are reachable from here";
    /// this answers "which PAIRS of atoms are jointly reachable from here", which is what a
    /// planner emitting conjunctive subgoals has to know before it emits one. Reconstructing it
    /// afterwards means unpacking every state again, so it is accumulated during search.
    ///
    /// Off by default: it costs one bitset union per set bit of every created state, i.e. a
    /// factor of |atoms per state| more work than the plain reached-atom union, and
    /// |reached atoms| bitsets of memory.
    ///
    /// Like the reached-atom union, this observes EVERY created state, including successors
    /// that a novelty test then prunes. Those states are genuinely reachable, so their pairs
    /// genuinely co-occur; a caller wanting only the states that entered a search tree has to
    /// reconstruct that from the search's own event handler.
    ///
    /// Must be called before any state is created; it is not retroactive.
    void enable_co_occurrence_tracking();

    /// @brief Fluent atom index -> the atoms ever true alongside it, itself included. Sized to
    /// the largest reached atom index plus one, so callers must bounds-check before indexing.
    /// Empty unless `enable_co_occurrence_tracking` was called.
    const std::vector<FlatBitset>& get_co_occurrence_by_atom() const;

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

    PackedState get_packed_state(Index state_index) const;

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
