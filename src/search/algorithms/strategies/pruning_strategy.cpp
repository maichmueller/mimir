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

#include "mimir/search/algorithms/strategies/pruning_strategy.hpp"

#include <stdexcept>

using namespace mimir::formalism;

namespace mimir::search
{
bool IPruningStrategy::supports_action_add_effect_precheck() const { return false; }

bool IPruningStrategy::should_bypass_action_add_effect_precheck(const State& state) const
{
    [[maybe_unused]] const auto& ignored_state = state;
    return false;
}

bool IPruningStrategy::precheck_requires_delete_effects() const { return false; }

bool IPruningStrategy::test_transition_novelty_from_add_effects(const State& state,
                                                                 const iw::AtomIndexList& add_fluent_atom_indices,
                                                                 const iw::AtomIndexList& del_fluent_atom_indices) const
{
    [[maybe_unused]] const auto& ignored_state = state;
    [[maybe_unused]] const auto& ignored_add_fluent_atom_indices = add_fluent_atom_indices;
    [[maybe_unused]] const auto& ignored_del_fluent_atom_indices = del_fluent_atom_indices;
    throw std::invalid_argument("IPruningStrategy does not support add-effect novelty prechecks.");
}

bool IPruningStrategy::consume_skip_state_expansion(const State& state)
{
    [[maybe_unused]] const auto& ignored_state = state;
    return false;
}

bool IPruningStrategy::supports_atom_novelty_query() const { return false; }

bool IPruningStrategy::test_atom_novelty_read_only(Index atom_index) const
{
    [[maybe_unused]] const auto ignored_atom_index = atom_index;
    throw std::invalid_argument("IPruningStrategy does not support atom-level novelty queries.");
}

bool IPruningStrategy::supports_transition_novel_witness_query() const { return false; }

void IPruningStrategy::compute_transition_novel_fluent_atom_indices_read_only(const State& state,
                                                                               const State& succ_state,
                                                                               iw::AtomIndexList& out_novel_fluent_atom_indices) const
{
    [[maybe_unused]] const auto& ignored_state = state;
    [[maybe_unused]] const auto& ignored_succ_state = succ_state;
    out_novel_fluent_atom_indices.clear();
    throw std::invalid_argument("IPruningStrategy does not support transition-level novel witness queries.");
}

bool IPruningStrategy::supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const
{
    return beam_novelty_mode == BeamNoveltyMode::ALL_TESTED;
}

bool IPruningStrategy::test_prune_successor_state_for_beam_selection(const State& state,
                                                                     const State& succ_state,
                                                                     bool is_new_succ,
                                                                     BeamNoveltyMode beam_novelty_mode)
{
    if (!supports_beam_novelty_mode(beam_novelty_mode))
    {
        throw std::invalid_argument("IPruningStrategy does not support the requested beam novelty mode.");
    }

    return test_prune_successor_state(state, succ_state, is_new_succ);
}

bool IPruningStrategy::supports_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const
{
    [[maybe_unused]] const auto ignored_beam_novelty_mode = beam_novelty_mode;
    return false;
}

bool IPruningStrategy::supports_relaxed_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const
{
    [[maybe_unused]] const auto ignored_beam_novelty_mode = beam_novelty_mode;
    return false;
}

bool IPruningStrategy::test_prune_staged_successor_state_for_beam_selection(const State& state,
                                                                            const FlatBitset& succ_fluent_atoms,
                                                                            const FlatBitset& succ_derived_atoms,
                                                                            const FlatDoubleList& succ_numeric_variables,
                                                                            const iw::AtomIndexList& succ_fluent_atom_indices,
                                                                            bool is_new_succ,
                                                                            BeamNoveltyMode beam_novelty_mode)
{
    [[maybe_unused]] const auto& ignored_state = state;
    [[maybe_unused]] const auto& ignored_succ_fluent_atoms = succ_fluent_atoms;
    [[maybe_unused]] const auto& ignored_succ_derived_atoms = succ_derived_atoms;
    [[maybe_unused]] const auto& ignored_succ_numeric_variables = succ_numeric_variables;
    [[maybe_unused]] const auto& ignored_succ_fluent_atom_indices = succ_fluent_atom_indices;
    [[maybe_unused]] const auto ignored_is_new_succ = is_new_succ;
    [[maybe_unused]] const auto ignored_beam_novelty_mode = beam_novelty_mode;
    throw std::invalid_argument("IPruningStrategy does not support staged beam pruning.");
}

bool IPruningStrategy::test_prune_staged_successor_state_for_relaxed_beam_selection(const State& state,
                                                                                    const FlatBitset& succ_fluent_atoms,
                                                                                    const FlatBitset& succ_derived_atoms,
                                                                                    const FlatDoubleList& succ_numeric_variables,
                                                                                    const iw::AtomIndexList& succ_fluent_atom_indices,
                                                                                    BeamNoveltyMode beam_novelty_mode)
{
    [[maybe_unused]] const auto& ignored_state = state;
    [[maybe_unused]] const auto& ignored_succ_fluent_atoms = succ_fluent_atoms;
    [[maybe_unused]] const auto& ignored_succ_derived_atoms = succ_derived_atoms;
    [[maybe_unused]] const auto& ignored_succ_numeric_variables = succ_numeric_variables;
    [[maybe_unused]] const auto& ignored_succ_fluent_atom_indices = succ_fluent_atom_indices;
    [[maybe_unused]] const auto ignored_beam_novelty_mode = beam_novelty_mode;
    throw std::invalid_argument("IPruningStrategy does not support relaxed staged beam pruning.");
}

void IPruningStrategy::on_begin_beam_replay(BeamNoveltyMode beam_novelty_mode)
{
    if (!supports_beam_novelty_mode(beam_novelty_mode))
    {
        throw std::invalid_argument("IPruningStrategy does not support the requested beam novelty mode.");
    }
}

bool IPruningStrategy::test_prune_successor_state_for_beam_replay(const State& state,
                                                                  const State& succ_state,
                                                                  bool is_new_succ,
                                                                  BeamNoveltyMode beam_novelty_mode)
{
    if (!supports_beam_novelty_mode(beam_novelty_mode))
    {
        throw std::invalid_argument("IPruningStrategy does not support the requested beam novelty mode.");
    }

    return test_prune_successor_state(state, succ_state, is_new_succ);
}

bool IPruningStrategy::test_prune_staged_successor_state_for_beam_replay(const State& state,
                                                                         const FlatBitset& succ_fluent_atoms,
                                                                         const FlatBitset& succ_derived_atoms,
                                                                         const FlatDoubleList& succ_numeric_variables,
                                                                         const iw::AtomIndexList& succ_fluent_atom_indices,
                                                                         bool is_new_succ,
                                                                         BeamNoveltyMode beam_novelty_mode)
{
    [[maybe_unused]] const auto& ignored_state = state;
    [[maybe_unused]] const auto& ignored_succ_fluent_atoms = succ_fluent_atoms;
    [[maybe_unused]] const auto& ignored_succ_derived_atoms = succ_derived_atoms;
    [[maybe_unused]] const auto& ignored_succ_numeric_variables = succ_numeric_variables;
    [[maybe_unused]] const auto& ignored_succ_fluent_atom_indices = succ_fluent_atom_indices;
    [[maybe_unused]] const auto ignored_is_new_succ = is_new_succ;
    [[maybe_unused]] const auto ignored_beam_novelty_mode = beam_novelty_mode;
    throw std::invalid_argument("IPruningStrategy does not support staged beam pruning.");
}

void IPruningStrategy::on_end_beam_replay(BeamNoveltyMode beam_novelty_mode)
{
    if (!supports_beam_novelty_mode(beam_novelty_mode))
    {
        throw std::invalid_argument("IPruningStrategy does not support the requested beam novelty mode.");
    }
}

/* NoPruningStrategyImpl */
bool NoPruningStrategyImpl::test_prune_initial_state(const State& state) { return false; }

bool NoPruningStrategyImpl::test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ) { return false; }

bool NoPruningStrategyImpl::supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const
{
    [[maybe_unused]] const auto ignored_beam_novelty_mode = beam_novelty_mode;
    return true;
}

bool NoPruningStrategyImpl::supports_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const
{
    [[maybe_unused]] const auto ignored_beam_novelty_mode = beam_novelty_mode;
    return true;
}

bool NoPruningStrategyImpl::supports_relaxed_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const
{
    [[maybe_unused]] const auto ignored_beam_novelty_mode = beam_novelty_mode;
    return true;
}

bool NoPruningStrategyImpl::test_prune_staged_successor_state_for_beam_selection(const State& state,
                                                                                  const FlatBitset& succ_fluent_atoms,
                                                                                  const FlatBitset& succ_derived_atoms,
                                                                                  const FlatDoubleList& succ_numeric_variables,
                                                                                  const iw::AtomIndexList& succ_fluent_atom_indices,
                                                                                  bool is_new_succ,
                                                                                  BeamNoveltyMode beam_novelty_mode)
{
    [[maybe_unused]] const auto& ignored_state = state;
    [[maybe_unused]] const auto& ignored_succ_fluent_atoms = succ_fluent_atoms;
    [[maybe_unused]] const auto& ignored_succ_derived_atoms = succ_derived_atoms;
    [[maybe_unused]] const auto& ignored_succ_numeric_variables = succ_numeric_variables;
    [[maybe_unused]] const auto& ignored_succ_fluent_atom_indices = succ_fluent_atom_indices;
    [[maybe_unused]] const auto ignored_is_new_succ = is_new_succ;
    [[maybe_unused]] const auto ignored_beam_novelty_mode = beam_novelty_mode;
    return false;
}

bool NoPruningStrategyImpl::test_prune_staged_successor_state_for_beam_replay(const State& state,
                                                                               const FlatBitset& succ_fluent_atoms,
                                                                               const FlatBitset& succ_derived_atoms,
                                                                               const FlatDoubleList& succ_numeric_variables,
                                                                               const iw::AtomIndexList& succ_fluent_atom_indices,
                                                                               bool is_new_succ,
                                                                               BeamNoveltyMode beam_novelty_mode)
{
    return test_prune_staged_successor_state_for_beam_selection(state,
                                                                succ_fluent_atoms,
                                                                succ_derived_atoms,
                                                                succ_numeric_variables,
                                                                succ_fluent_atom_indices,
                                                                is_new_succ,
                                                                beam_novelty_mode);
}

bool NoPruningStrategyImpl::test_prune_staged_successor_state_for_relaxed_beam_selection(const State& state,
                                                                                          const FlatBitset& succ_fluent_atoms,
                                                                                          const FlatBitset& succ_derived_atoms,
                                                                                          const FlatDoubleList& succ_numeric_variables,
                                                                                          const iw::AtomIndexList& succ_fluent_atom_indices,
                                                                                          BeamNoveltyMode beam_novelty_mode)
{
    return test_prune_staged_successor_state_for_beam_selection(state,
                                                                succ_fluent_atoms,
                                                                succ_derived_atoms,
                                                                succ_numeric_variables,
                                                                succ_fluent_atom_indices,
                                                                true,
                                                                beam_novelty_mode);
}

NoPruningStrategy NoPruningStrategyImpl::create() { return std::make_shared<NoPruningStrategyImpl>(); }

/* DuplicatePruningStrategyImpl */
bool DuplicatePruningStrategyImpl::test_prune_initial_state(const State& state) { return false; };

bool DuplicatePruningStrategyImpl::test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ) { return !is_new_succ; }

bool DuplicatePruningStrategyImpl::supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const
{
    [[maybe_unused]] const auto ignored_beam_novelty_mode = beam_novelty_mode;
    return true;
}

bool DuplicatePruningStrategyImpl::supports_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const
{
    [[maybe_unused]] const auto ignored_beam_novelty_mode = beam_novelty_mode;
    return true;
}

bool DuplicatePruningStrategyImpl::supports_relaxed_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const
{
    [[maybe_unused]] const auto ignored_beam_novelty_mode = beam_novelty_mode;
    return true;
}

bool DuplicatePruningStrategyImpl::test_prune_staged_successor_state_for_beam_selection(const State& state,
                                                                                         const FlatBitset& succ_fluent_atoms,
                                                                                         const FlatBitset& succ_derived_atoms,
                                                                                         const FlatDoubleList& succ_numeric_variables,
                                                                                         const iw::AtomIndexList& succ_fluent_atom_indices,
                                                                                         bool is_new_succ,
                                                                                         BeamNoveltyMode beam_novelty_mode)
{
    [[maybe_unused]] const auto& ignored_state = state;
    [[maybe_unused]] const auto& ignored_succ_fluent_atoms = succ_fluent_atoms;
    [[maybe_unused]] const auto& ignored_succ_derived_atoms = succ_derived_atoms;
    [[maybe_unused]] const auto& ignored_succ_numeric_variables = succ_numeric_variables;
    [[maybe_unused]] const auto& ignored_succ_fluent_atom_indices = succ_fluent_atom_indices;
    [[maybe_unused]] const auto ignored_beam_novelty_mode = beam_novelty_mode;
    return !is_new_succ;
}

bool DuplicatePruningStrategyImpl::test_prune_staged_successor_state_for_beam_replay(const State& state,
                                                                                      const FlatBitset& succ_fluent_atoms,
                                                                                      const FlatBitset& succ_derived_atoms,
                                                                                      const FlatDoubleList& succ_numeric_variables,
                                                                                      const iw::AtomIndexList& succ_fluent_atom_indices,
                                                                                      bool is_new_succ,
                                                                                      BeamNoveltyMode beam_novelty_mode)
{
    return test_prune_staged_successor_state_for_beam_selection(state,
                                                                succ_fluent_atoms,
                                                                succ_derived_atoms,
                                                                succ_numeric_variables,
                                                                succ_fluent_atom_indices,
                                                                is_new_succ,
                                                                beam_novelty_mode);
}

bool DuplicatePruningStrategyImpl::test_prune_staged_successor_state_for_relaxed_beam_selection(const State& state,
                                                                                                const FlatBitset& succ_fluent_atoms,
                                                                                                const FlatBitset& succ_derived_atoms,
                                                                                                const FlatDoubleList& succ_numeric_variables,
                                                                                                const iw::AtomIndexList& succ_fluent_atom_indices,
                                                                                                BeamNoveltyMode beam_novelty_mode)
{
    [[maybe_unused]] const auto& ignored_state = state;
    [[maybe_unused]] const auto& ignored_succ_fluent_atoms = succ_fluent_atoms;
    [[maybe_unused]] const auto& ignored_succ_derived_atoms = succ_derived_atoms;
    [[maybe_unused]] const auto& ignored_succ_numeric_variables = succ_numeric_variables;
    [[maybe_unused]] const auto& ignored_succ_fluent_atom_indices = succ_fluent_atom_indices;
    [[maybe_unused]] const auto ignored_beam_novelty_mode = beam_novelty_mode;
    return false;
}

DuplicatePruningStrategy DuplicatePruningStrategyImpl::create() { return std::make_shared<DuplicatePruningStrategyImpl>(); }
}
