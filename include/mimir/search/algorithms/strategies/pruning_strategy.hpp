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

#ifndef MIMIR_SEARCH_ALGORITHMS_STRATEGIES_PRUNING_STRATEGY_HPP_
#define MIMIR_SEARCH_ALGORITHMS_STRATEGIES_PRUNING_STRATEGY_HPP_

#include "mimir/common/types_cista.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/search/declarations.hpp"

namespace mimir::search
{

/// @brief `IPruningStrategy` encapsulates logic to test whether a newly generated state must be pruned or not.
class IPruningStrategy
{
public:
    virtual ~IPruningStrategy() = default;

    virtual bool test_prune_initial_state(const State& state) = 0;
    virtual bool test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ) = 0;
    virtual bool supports_action_add_effect_precheck() const;
    virtual bool should_bypass_action_add_effect_precheck(const State& state) const;

    /// @brief Whether the precheck needs the transition's DELETE effects as well as its adds.
    ///
    /// Atom-level novelty does not: only an added atom can carry a new tuple, so what an action
    /// removes cannot make a transition novel. Landmark-restricted novelty does, because its
    /// feature is a pair `(landmark coordinate, free tuple)` and the coordinate set of the
    /// successor is `(coords(s) \ deleted landmarks) | added landmarks`. Without the deletes a
    /// caller cannot tell which coordinates the successor keeps, nor whether the `BOT` coordinate
    /// flips on because the last true landmark was removed.
    ///
    /// A strategy answering true also opts into the caller's cached-effect fast path: for actions
    /// whose fluent effects are all unconditional the caller passes the state-independent effect
    /// sets intersected against the state, without re-deriving them per state.
    virtual bool precheck_requires_delete_effects() const;

    /// @brief Whether the transition described by its atom-level delta could survive novelty
    /// pruning. May answer true for a transition that would in fact be pruned; must never answer
    /// false for one that would survive.
    ///
    /// @param add_fluent_atom_indices the atoms that become true, i.e. already filtered against
    /// `state`, sorted ascending.
    /// @param del_fluent_atom_indices the atoms that become false, likewise filtered and sorted.
    /// Empty and ignored unless `precheck_requires_delete_effects` holds.
    virtual bool test_transition_novelty_from_add_effects(const State& state,
                                                          const iw::AtomIndexList& add_fluent_atom_indices,
                                                          const iw::AtomIndexList& del_fluent_atom_indices) const;
    virtual bool consume_skip_state_expansion(const State& state);
    virtual bool supports_atom_novelty_query() const;
    virtual bool test_atom_novelty_read_only(Index atom_index) const;
    virtual bool supports_transition_novel_witness_query() const;
    virtual void compute_transition_novel_fluent_atom_indices_read_only(const State& state,
                                                                        const State& succ_state,
                                                                        iw::AtomIndexList& out_novel_fluent_atom_indices) const;

    virtual bool supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const;
    virtual bool test_prune_successor_state_for_beam_selection(const State& state,
                                                               const State& succ_state,
                                                               bool is_new_succ,
                                                               BeamNoveltyMode beam_novelty_mode);
    virtual bool supports_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const;
    virtual bool supports_relaxed_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const;
    virtual bool test_prune_staged_successor_state_for_beam_selection(const State& state,
                                                                      const FlatBitset& succ_fluent_atoms,
                                                                      const FlatBitset& succ_derived_atoms,
                                                                      const FlatDoubleList& succ_numeric_variables,
                                                                      const iw::AtomIndexList& succ_fluent_atom_indices,
                                                                      bool is_new_succ,
                                                                      BeamNoveltyMode beam_novelty_mode);
    virtual bool test_prune_staged_successor_state_for_relaxed_beam_selection(const State& state,
                                                                              const FlatBitset& succ_fluent_atoms,
                                                                              const FlatBitset& succ_derived_atoms,
                                                                              const FlatDoubleList& succ_numeric_variables,
                                                                              const iw::AtomIndexList& succ_fluent_atom_indices,
                                                                              BeamNoveltyMode beam_novelty_mode);
    virtual void on_begin_beam_replay(BeamNoveltyMode beam_novelty_mode);
    virtual bool test_prune_successor_state_for_beam_replay(const State& state,
                                                            const State& succ_state,
                                                            bool is_new_succ,
                                                            BeamNoveltyMode beam_novelty_mode);
    virtual bool test_prune_staged_successor_state_for_beam_replay(const State& state,
                                                                   const FlatBitset& succ_fluent_atoms,
                                                                   const FlatBitset& succ_derived_atoms,
                                                                   const FlatDoubleList& succ_numeric_variables,
                                                                   const iw::AtomIndexList& succ_fluent_atom_indices,
                                                                   bool is_new_succ,
                                                                   BeamNoveltyMode beam_novelty_mode);
    virtual void on_end_beam_replay(BeamNoveltyMode beam_novelty_mode);
};

/// @brief `NoPruningStrategyImpl` never prunes a newly generated state.
class NoPruningStrategyImpl : public IPruningStrategy
{
public:
    bool test_prune_initial_state(const State& state) override;
    bool test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ) override;
    bool supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const override;
    bool supports_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const override;
    bool supports_relaxed_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const override;
    bool test_prune_staged_successor_state_for_beam_selection(const State& state,
                                                              const FlatBitset& succ_fluent_atoms,
                                                              const FlatBitset& succ_derived_atoms,
                                                              const FlatDoubleList& succ_numeric_variables,
                                                              const iw::AtomIndexList& succ_fluent_atom_indices,
                                                              bool is_new_succ,
                                                              BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_staged_successor_state_for_relaxed_beam_selection(const State& state,
                                                                      const FlatBitset& succ_fluent_atoms,
                                                                      const FlatBitset& succ_derived_atoms,
                                                                      const FlatDoubleList& succ_numeric_variables,
                                                                      const iw::AtomIndexList& succ_fluent_atom_indices,
                                                                      BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_staged_successor_state_for_beam_replay(const State& state,
                                                           const FlatBitset& succ_fluent_atoms,
                                                           const FlatBitset& succ_derived_atoms,
                                                           const FlatDoubleList& succ_numeric_variables,
                                                           const iw::AtomIndexList& succ_fluent_atom_indices,
                                                           bool is_new_succ,
                                                           BeamNoveltyMode beam_novelty_mode) override;

    static NoPruningStrategy create();
};

/// @brief `DuplicatePruningStrategyImpl` prunes a newly generated state if it was already generated before.
class DuplicatePruningStrategyImpl : public IPruningStrategy
{
public:
    bool test_prune_initial_state(const State& state) override;
    bool test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ) override;
    bool supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const override;
    bool supports_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const override;
    bool supports_relaxed_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const override;
    bool test_prune_staged_successor_state_for_beam_selection(const State& state,
                                                              const FlatBitset& succ_fluent_atoms,
                                                              const FlatBitset& succ_derived_atoms,
                                                              const FlatDoubleList& succ_numeric_variables,
                                                              const iw::AtomIndexList& succ_fluent_atom_indices,
                                                              bool is_new_succ,
                                                              BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_staged_successor_state_for_relaxed_beam_selection(const State& state,
                                                                      const FlatBitset& succ_fluent_atoms,
                                                                      const FlatBitset& succ_derived_atoms,
                                                                      const FlatDoubleList& succ_numeric_variables,
                                                                      const iw::AtomIndexList& succ_fluent_atom_indices,
                                                                      BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_staged_successor_state_for_beam_replay(const State& state,
                                                           const FlatBitset& succ_fluent_atoms,
                                                           const FlatBitset& succ_derived_atoms,
                                                           const FlatDoubleList& succ_numeric_variables,
                                                           const iw::AtomIndexList& succ_fluent_atom_indices,
                                                           bool is_new_succ,
                                                           BeamNoveltyMode beam_novelty_mode) override;

    static DuplicatePruningStrategy create();
};
}

#endif
