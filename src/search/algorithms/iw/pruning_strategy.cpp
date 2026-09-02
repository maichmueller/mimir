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

#include "mimir/search/algorithms/iw/pruning_strategy.hpp"

#include "mimir/formalism/problem.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"
#include "mimir/search/state.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>
#include <stdexcept>

using namespace mimir::formalism;

namespace mimir::search::iw
{
namespace
{
bool is_staged_self_loop(const State& state,
                         const FlatBitset& succ_fluent_atoms,
                         const FlatBitset& succ_derived_atoms,
                         const FlatDoubleList& succ_numeric_variables)
{
    return state.get_atoms<FluentTag>() == succ_fluent_atoms && state.get_atoms<DerivedTag>() == succ_derived_atoms
           && state.get_numeric_variables() == succ_numeric_variables;
}
}

ArityZeroNoveltyPruningStrategyImpl::ArityZeroNoveltyPruningStrategyImpl(State initial_state) : m_initial_state(std::move(initial_state)) {}

PruningStrategy ArityZeroNoveltyPruningStrategyImpl::create(State initial_state)
{
    return std::make_shared<ArityZeroNoveltyPruningStrategyImpl>(std::move(initial_state));
}

bool ArityZeroNoveltyPruningStrategyImpl::test_prune_initial_state(const State& state) { return false; }

bool ArityZeroNoveltyPruningStrategyImpl::test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ)
{
    return state != m_initial_state || state == succ_state;
}

bool ArityZeroNoveltyPruningStrategyImpl::supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const
{
    return beam_novelty_mode == BeamNoveltyMode::ALL_TESTED || beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY;
}

bool ArityZeroNoveltyPruningStrategyImpl::supports_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const
{
    return supports_beam_novelty_mode(beam_novelty_mode);
}

bool ArityZeroNoveltyPruningStrategyImpl::supports_relaxed_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const
{
    return beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY;
}

bool ArityZeroNoveltyPruningStrategyImpl::test_prune_successor_state_for_beam_selection(const State& state,
                                                                                         const State& succ_state,
                                                                                         bool is_new_succ,
                                                                                         BeamNoveltyMode beam_novelty_mode)
{
    if (beam_novelty_mode == BeamNoveltyMode::ALL_TESTED)
    {
        return test_prune_successor_state(state, succ_state, is_new_succ);
    }

    return state != m_initial_state || !is_new_succ;
}

bool ArityZeroNoveltyPruningStrategyImpl::test_prune_staged_successor_state_for_beam_selection(const State& state,
                                                                                                const FlatBitset& succ_fluent_atoms,
                                                                                                const FlatBitset& succ_derived_atoms,
                                                                                                const FlatDoubleList& succ_numeric_variables,
                                                                                                const AtomIndexList& succ_fluent_atom_indices,
                                                                                                bool is_new_succ,
                                                                                                BeamNoveltyMode beam_novelty_mode)
{
    [[maybe_unused]] const auto& ignored_succ_fluent_atom_indices = succ_fluent_atom_indices;

    if (beam_novelty_mode == BeamNoveltyMode::ALL_TESTED)
    {
        return state.get_index() != m_initial_state.get_index() || is_staged_self_loop(state, succ_fluent_atoms, succ_derived_atoms, succ_numeric_variables);
    }

    return state.get_index() != m_initial_state.get_index() || !is_new_succ;
}

bool ArityZeroNoveltyPruningStrategyImpl::test_prune_staged_successor_state_for_beam_replay(const State& state,
                                                                                             const FlatBitset& succ_fluent_atoms,
                                                                                             const FlatBitset& succ_derived_atoms,
                                                                                             const FlatDoubleList& succ_numeric_variables,
                                                                                             const AtomIndexList& succ_fluent_atom_indices,
                                                                                             bool is_new_succ,
                                                                                             BeamNoveltyMode beam_novelty_mode)
{
    [[maybe_unused]] const auto& ignored_succ_fluent_atom_indices = succ_fluent_atom_indices;
    [[maybe_unused]] const auto ignored_is_new_succ = is_new_succ;

    if (beam_novelty_mode == BeamNoveltyMode::ALL_TESTED)
    {
        return test_prune_staged_successor_state_for_beam_selection(state,
                                                                    succ_fluent_atoms,
                                                                    succ_derived_atoms,
                                                                    succ_numeric_variables,
                                                                    succ_fluent_atom_indices,
                                                                    is_new_succ,
                                                                    beam_novelty_mode);
    }

    return state.get_index() != m_initial_state.get_index() || is_staged_self_loop(state, succ_fluent_atoms, succ_derived_atoms, succ_numeric_variables);
}

bool ArityZeroNoveltyPruningStrategyImpl::test_prune_staged_successor_state_for_relaxed_beam_selection(const State& state,
                                                                                                        const FlatBitset& succ_fluent_atoms,
                                                                                                        const FlatBitset& succ_derived_atoms,
                                                                                                        const FlatDoubleList& succ_numeric_variables,
                                                                                                        const AtomIndexList& succ_fluent_atom_indices,
                                                                                                        BeamNoveltyMode beam_novelty_mode)
{
    [[maybe_unused]] const auto& ignored_succ_fluent_atom_indices = succ_fluent_atom_indices;

    if (beam_novelty_mode != BeamNoveltyMode::SURVIVORS_ONLY)
    {
        throw std::invalid_argument("ArityZeroNoveltyPruningStrategyImpl only supports relaxed staged beam selection in SURVIVORS_ONLY mode.");
    }

    return state.get_index() != m_initial_state.get_index() || is_staged_self_loop(state, succ_fluent_atoms, succ_derived_atoms, succ_numeric_variables);
}

size_t ArityKNoveltyPruningStrategyImpl::AtomIndexListHash::operator()(const AtomIndexList& atom_indices) const noexcept
{
    auto seed = atom_indices.size();
    for (const auto atom_index : atom_indices)
    {
        seed ^= std::hash<AtomIndex> {}(atom_index) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
}

ArityKNoveltyPruningStrategyImpl::ArityKNoveltyPruningStrategyImpl(size_t arity, size_t num_atoms, bool optimize_root_depth_one_continuation) :
    m_novelty_table(arity, num_atoms),
    m_optimize_root_depth_one_continuation(optimize_root_depth_one_continuation),
    m_root_state_index(std::nullopt),
    m_beam_layer_delta_tuples(),
    m_beam_layer_delta_tuple_set(),
    m_scratch_novel_tuples(),
    m_scratch_atom_indices_key(),
    m_skip_depth_one_expansion_state_indices(),
    m_skip_depth_one_expansion_fluent_atom_indices_fallback()
{
}

PruningStrategy ArityKNoveltyPruningStrategyImpl::create(size_t arity, size_t num_atoms, bool optimize_root_depth_one_continuation)
{
    return std::make_shared<ArityKNoveltyPruningStrategyImpl>(arity, num_atoms, optimize_root_depth_one_continuation);
}

bool ArityKNoveltyPruningStrategyImpl::test_transition_novelty(const State& state, const State& succ_state)
{
    return m_novelty_table.test_novelty_read_only(state, succ_state);
}

bool ArityKNoveltyPruningStrategyImpl::test_transition_novelty_and_update_delta(const State& state, const State& succ_state)
{
    m_novelty_table.compute_novel_tuples(state, succ_state, m_scratch_novel_tuples);

    bool is_novel = false;
    for (const auto& tuple : m_scratch_novel_tuples)
    {
        if (m_beam_layer_delta_tuple_set.emplace(tuple).second)
        {
            m_beam_layer_delta_tuples.push_back(tuple);
            is_novel = true;
        }
    }
    return is_novel;
}

bool ArityKNoveltyPruningStrategyImpl::test_prune_initial_state(const State& state)
{
    if (m_optimize_root_depth_one_continuation && (m_novelty_table.get_tuple_index_mapper().get_arity() == 1) && !m_root_state_index)
    {
        m_root_state_index = state.get_index();
    }

    return !m_novelty_table.test_novelty_and_update_table(state);
}

bool ArityKNoveltyPruningStrategyImpl::test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ)
{
    if (state == succ_state)
    {
        return true;
    }

    if (!is_new_succ)
    {
        // Transition novelty depends on the predecessor as well. A duplicate successor
        // can still expose a novel transition, but duplicate states are pruned either way.
        return true;
    }

    const auto is_novel = m_novelty_table.test_novelty_and_update_table(state, succ_state);
    if (m_optimize_root_depth_one_continuation && m_root_state_index.has_value() && (state.get_index() == *m_root_state_index))
    {
        if (!is_novel)
        {
            m_skip_depth_one_expansion_state_indices.emplace(succ_state.get_index());
        }
        return false;
    }

    return !is_novel;
}

bool ArityKNoveltyPruningStrategyImpl::supports_action_add_effect_precheck() const { return true; }

bool ArityKNoveltyPruningStrategyImpl::should_bypass_action_add_effect_precheck(const State& state) const
{
    return m_optimize_root_depth_one_continuation && m_root_state_index.has_value() && (state.get_index() == *m_root_state_index);
}

bool ArityKNoveltyPruningStrategyImpl::test_transition_novelty_from_add_effects(const State& state,
                                                                                 const AtomIndexList& add_fluent_atom_indices,
                                                                                 const AtomIndexList& del_fluent_atom_indices) const
{
    /* Atom-level novelty only ever grows: a tuple is novel because the transition ADDS an atom, so
       what the action removes cannot make it novel, and cannot make a novel tuple stale either.
       Hence `precheck_requires_delete_effects()` stays false here and the list is not consulted. */
    [[maybe_unused]] const auto& ignored_del_fluent_atom_indices = del_fluent_atom_indices;

    if (should_bypass_action_add_effect_precheck(state))
    {
        return true;
    }

    return m_novelty_table.test_novelty_read_only(state, add_fluent_atom_indices);
}

bool ArityKNoveltyPruningStrategyImpl::consume_skip_state_expansion(const State& state)
{
    if (!m_skip_depth_one_expansion_state_indices.empty() && m_skip_depth_one_expansion_state_indices.erase(state.get_index()) > 0)
    {
        return true;
    }

    if (m_skip_depth_one_expansion_fluent_atom_indices_fallback.empty())
    {
        return false;
    }

    m_scratch_atom_indices_key.assign(state.get_atoms<FluentTag>().begin(), state.get_atoms<FluentTag>().end());
    return m_skip_depth_one_expansion_fluent_atom_indices_fallback.erase(m_scratch_atom_indices_key) > 0;
}

bool ArityKNoveltyPruningStrategyImpl::supports_atom_novelty_query() const
{
    return m_novelty_table.get_tuple_index_mapper().get_arity() == 1;
}

bool ArityKNoveltyPruningStrategyImpl::test_atom_novelty_read_only(Index atom_index) const
{
    if (!supports_atom_novelty_query())
    {
        throw std::invalid_argument("ArityKNoveltyPruningStrategyImpl::test_atom_novelty_read_only only supports arity 1.");
    }
    return m_novelty_table.test_atom_novelty_read_only(atom_index);
}

bool ArityKNoveltyPruningStrategyImpl::supports_transition_novel_witness_query() const { return supports_atom_novelty_query(); }

void ArityKNoveltyPruningStrategyImpl::compute_transition_novel_fluent_atom_indices_read_only(const State& state,
                                                                                               const State& succ_state,
                                                                                               AtomIndexList& out_novel_fluent_atom_indices) const
{
    if (!supports_transition_novel_witness_query())
    {
        throw std::invalid_argument("ArityKNoveltyPruningStrategyImpl transition witness query only supports arity 1.");
    }

    out_novel_fluent_atom_indices.clear();
    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    const auto& succ_state_fluent_atoms = succ_state.get_atoms<FluentTag>();

    auto it_state = state_fluent_atoms.begin();
    auto it_succ_state = succ_state_fluent_atoms.begin();
    for (; (it_state != state_fluent_atoms.end()) && (it_succ_state != succ_state_fluent_atoms.end());)
    {
        if (*it_state < *it_succ_state)
        {
            ++it_state;
        }
        else if (*it_state > *it_succ_state)
        {
            if (m_novelty_table.test_atom_novelty_read_only(*it_succ_state))
            {
                out_novel_fluent_atom_indices.push_back(*it_succ_state);
            }
            ++it_succ_state;
        }
        else
        {
            ++it_state;
            ++it_succ_state;
        }
    }
    for (; it_succ_state != succ_state_fluent_atoms.end(); ++it_succ_state)
    {
        if (m_novelty_table.test_atom_novelty_read_only(*it_succ_state))
        {
            out_novel_fluent_atom_indices.push_back(*it_succ_state);
        }
    }
}

bool ArityKNoveltyPruningStrategyImpl::supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const
{
    return beam_novelty_mode == BeamNoveltyMode::ALL_TESTED || beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY;
}

bool ArityKNoveltyPruningStrategyImpl::supports_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const
{
    return supports_beam_novelty_mode(beam_novelty_mode);
}

bool ArityKNoveltyPruningStrategyImpl::supports_relaxed_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const
{
    return beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY;
}

bool ArityKNoveltyPruningStrategyImpl::test_prune_successor_state_for_beam_selection(const State& state,
                                                                                      const State& succ_state,
                                                                                      bool is_new_succ,
                                                                                      BeamNoveltyMode beam_novelty_mode)
{
    if (beam_novelty_mode == BeamNoveltyMode::ALL_TESTED)
    {
        return test_prune_successor_state(state, succ_state, is_new_succ);
    }

    if (!is_new_succ)
    {
        return true;
    }

    const auto is_novel = test_transition_novelty(state, succ_state);
    if (m_optimize_root_depth_one_continuation && m_root_state_index.has_value() && (state.get_index() == *m_root_state_index))
    {
        if (!is_novel)
        {
            m_skip_depth_one_expansion_state_indices.emplace(succ_state.get_index());
        }
        return false;
    }

    return !is_novel;
}

bool ArityKNoveltyPruningStrategyImpl::test_prune_staged_successor_state_for_beam_selection(const State& state,
                                                                                             const FlatBitset& succ_fluent_atoms,
                                                                                             const FlatBitset& succ_derived_atoms,
                                                                                             const FlatDoubleList& succ_numeric_variables,
                                                                                             const AtomIndexList& succ_fluent_atom_indices,
                                                                                             bool is_new_succ,
                                                                                             BeamNoveltyMode beam_novelty_mode)
{
    [[maybe_unused]] const auto& ignored_succ_fluent_atoms = succ_fluent_atoms;
    [[maybe_unused]] const auto& ignored_succ_derived_atoms = succ_derived_atoms;
    [[maybe_unused]] const auto& ignored_succ_numeric_variables = succ_numeric_variables;

    if (beam_novelty_mode == BeamNoveltyMode::ALL_TESTED)
    {
        if (is_staged_self_loop(state, succ_fluent_atoms, succ_derived_atoms, succ_numeric_variables))
        {
            return true;
        }

        if (!is_new_succ)
        {
            return true;
        }

        const auto is_novel = m_novelty_table.test_novelty_and_update_table(state, succ_fluent_atom_indices);
        if (m_optimize_root_depth_one_continuation && m_root_state_index.has_value() && (state.get_index() == *m_root_state_index))
        {
            if (!is_novel)
            {
                m_skip_depth_one_expansion_fluent_atom_indices_fallback.emplace(succ_fluent_atom_indices.begin(), succ_fluent_atom_indices.end());
            }
            return false;
        }

        return !is_novel;
    }

    if (!is_new_succ)
    {
        return true;
    }

    const auto is_novel = m_novelty_table.test_novelty_read_only(state, succ_fluent_atom_indices);
    if (m_optimize_root_depth_one_continuation && m_root_state_index.has_value() && (state.get_index() == *m_root_state_index))
    {
        if (!is_novel)
        {
            m_skip_depth_one_expansion_fluent_atom_indices_fallback.emplace(succ_fluent_atom_indices.begin(), succ_fluent_atom_indices.end());
        }
        return false;
    }

    return !is_novel;
}

void ArityKNoveltyPruningStrategyImpl::on_begin_beam_replay(BeamNoveltyMode beam_novelty_mode)
{
    if (beam_novelty_mode != BeamNoveltyMode::SURVIVORS_ONLY)
    {
        return;
    }

    m_beam_layer_delta_tuples.clear();
    m_beam_layer_delta_tuple_set.clear();
    m_scratch_novel_tuples.clear();
}

bool ArityKNoveltyPruningStrategyImpl::test_prune_successor_state_for_beam_replay(const State& state,
                                                                                   const State& succ_state,
                                                                                   bool is_new_succ,
                                                                                   BeamNoveltyMode beam_novelty_mode)
{
    if (beam_novelty_mode == BeamNoveltyMode::ALL_TESTED)
    {
        return test_prune_successor_state(state, succ_state, is_new_succ);
    }

    if (state == succ_state)
    {
        return true;
    }

    const auto is_novel = test_transition_novelty_and_update_delta(state, succ_state);
    if (m_optimize_root_depth_one_continuation && m_root_state_index.has_value() && (state.get_index() == *m_root_state_index))
    {
        if (!is_novel)
        {
            m_skip_depth_one_expansion_state_indices.emplace(succ_state.get_index());
        }
        return false;
    }

    return !is_novel;
}

bool ArityKNoveltyPruningStrategyImpl::test_prune_staged_successor_state_for_beam_replay(const State& state,
                                                                                          const FlatBitset& succ_fluent_atoms,
                                                                                          const FlatBitset& succ_derived_atoms,
                                                                                          const FlatDoubleList& succ_numeric_variables,
                                                                                          const AtomIndexList& succ_fluent_atom_indices,
                                                                                          bool is_new_succ,
                                                                                          BeamNoveltyMode beam_novelty_mode)
{
    [[maybe_unused]] const auto& ignored_succ_fluent_atoms = succ_fluent_atoms;
    [[maybe_unused]] const auto& ignored_succ_derived_atoms = succ_derived_atoms;
    [[maybe_unused]] const auto& ignored_succ_numeric_variables = succ_numeric_variables;
    [[maybe_unused]] const auto ignored_is_new_succ = is_new_succ;

    if (beam_novelty_mode == BeamNoveltyMode::ALL_TESTED)
    {
        return test_prune_staged_successor_state_for_beam_selection(state,
                                                                    succ_fluent_atoms,
                                                                    succ_derived_atoms,
                                                                    succ_numeric_variables,
                                                                    succ_fluent_atom_indices,
                                                                    is_new_succ,
                                                                    beam_novelty_mode);
    }

    if (is_staged_self_loop(state, succ_fluent_atoms, succ_derived_atoms, succ_numeric_variables))
    {
        return true;
    }

    m_novelty_table.compute_novel_tuples(state, succ_fluent_atom_indices, m_scratch_novel_tuples);

    bool is_novel = false;
    for (const auto& tuple : m_scratch_novel_tuples)
    {
        if (m_beam_layer_delta_tuple_set.emplace(tuple).second)
        {
            m_beam_layer_delta_tuples.push_back(tuple);
            is_novel = true;
        }
    }
    if (m_optimize_root_depth_one_continuation && m_root_state_index.has_value() && (state.get_index() == *m_root_state_index))
    {
        if (!is_novel)
        {
            m_skip_depth_one_expansion_fluent_atom_indices_fallback.emplace(succ_fluent_atom_indices.begin(), succ_fluent_atom_indices.end());
        }
        return false;
    }

    return !is_novel;
}

bool ArityKNoveltyPruningStrategyImpl::test_prune_staged_successor_state_for_relaxed_beam_selection(const State& state,
                                                                                                     const FlatBitset& succ_fluent_atoms,
                                                                                                     const FlatBitset& succ_derived_atoms,
                                                                                                     const FlatDoubleList& succ_numeric_variables,
                                                                                                     const AtomIndexList& succ_fluent_atom_indices,
                                                                                                     BeamNoveltyMode beam_novelty_mode)
{
    [[maybe_unused]] const auto& ignored_succ_derived_atoms = succ_derived_atoms;
    [[maybe_unused]] const auto& ignored_succ_numeric_variables = succ_numeric_variables;

    if (beam_novelty_mode != BeamNoveltyMode::SURVIVORS_ONLY)
    {
        throw std::invalid_argument("ArityKNoveltyPruningStrategyImpl only supports relaxed staged beam selection in SURVIVORS_ONLY mode.");
    }

    if (is_staged_self_loop(state, succ_fluent_atoms, succ_derived_atoms, succ_numeric_variables))
    {
        return true;
    }

    const auto is_novel = m_novelty_table.test_novelty_read_only(state, succ_fluent_atom_indices);
    if (m_optimize_root_depth_one_continuation && m_root_state_index.has_value() && (state.get_index() == *m_root_state_index))
    {
        if (!is_novel)
        {
            m_skip_depth_one_expansion_fluent_atom_indices_fallback.emplace(succ_fluent_atom_indices.begin(), succ_fluent_atom_indices.end());
        }
        return false;
    }

    return !is_novel;
}

void ArityKNoveltyPruningStrategyImpl::on_end_beam_replay(BeamNoveltyMode beam_novelty_mode)
{
    if (beam_novelty_mode != BeamNoveltyMode::SURVIVORS_ONLY)
    {
        return;
    }

    m_novelty_table.insert_tuples(m_beam_layer_delta_tuples);
    m_beam_layer_delta_tuples.clear();
    m_beam_layer_delta_tuple_set.clear();
    m_scratch_novel_tuples.clear();
}

LandmarkNoveltyPruningStrategyImpl::LandmarkNoveltyPruningStrategyImpl(const landmarks::FactLandmarkGraph& landmarks,
                                                                       size_t arity,
                                                                       size_t num_atoms,
                                                                       LandmarkNoveltyTableOptions table_options,
                                                                       LandmarkGrouping grouping) :
    m_novelty_table(landmarks ? AtomIndexList(landmarks->get_landmark_atom_indices().begin(), landmarks->get_landmark_atom_indices().end()) : AtomIndexList {},
                    std::move(grouping),
                    arity,
                    num_atoms,
                    table_options)
{
}

PruningStrategy LandmarkNoveltyPruningStrategyImpl::create(const landmarks::FactLandmarkGraph& landmarks,
                                                           size_t arity,
                                                           size_t num_atoms,
                                                           LandmarkNoveltyTableOptions table_options,
                                                           LandmarkGrouping grouping)
{
    return std::make_shared<LandmarkNoveltyPruningStrategyImpl>(landmarks, arity, num_atoms, table_options, std::move(grouping));
}

LandmarkGrouping
LandmarkNoveltyPruningStrategyImpl::make_grouping(const landmarks::FactLandmarkGraph& landmarks,
                                                  bool disjunctive,
                                                  const IndexSet& unshared_atom_indices,
                                                  bool all_private)
{
    auto grouping = LandmarkGrouping {};
    if (!disjunctive || !landmarks)
    {
        return grouping;  // one row per fact landmark, exactly as before disjunctive landmarks
    }
    if (all_private && !unshared_atom_indices.empty())
    {
        throw std::invalid_argument("LandmarkNoveltyPruningStrategyImpl::make_grouping: all_private cannot be combined with unshared_atom_indices.");
    }
    for (const auto& members : landmarks->get_disjunctive_landmarks())
    {
        grouping.disjunctive_landmarks.emplace_back(members.begin(), members.end());
    }
    grouping.unshared_atom_indices = unshared_atom_indices;
    if (all_private)
    {
        grouping.mode = LandmarkGroupingMode::ALL_PRIVATE;
    }
    return grouping;
}

bool LandmarkNoveltyPruningStrategyImpl::test_prune_initial_state(const State& state) { return !m_novelty_table.test_novelty_and_update_table(state); }

bool LandmarkNoveltyPruningStrategyImpl::test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ)
{
    if (state == succ_state)
    {
        return true;
    }

    if (!is_new_succ)
    {
        // Same reasoning as `ArityKNoveltyPruningStrategyImpl`: transition novelty depends on the
        // predecessor, so a duplicate successor can still be novel, but it is pruned either way.
        return true;
    }

    return !m_novelty_table.test_novelty_and_update_table(state, succ_state);
}

bool LandmarkNoveltyPruningStrategyImpl::supports_action_add_effect_precheck() const
{
    /* The witness half of the precheck decomposes tuples into single atoms, which is only
       meaningful at arity 1 -- the same limit `ArityKNoveltyPruningStrategyImpl` puts on its
       atom-level queries. Wider passes fall back to testing the successor itself. */
    return m_novelty_table.get_tuple_index_mapper().get_arity() == 1;
}

bool LandmarkNoveltyPruningStrategyImpl::precheck_requires_delete_effects() const { return true; }

bool LandmarkNoveltyPruningStrategyImpl::test_transition_novelty_from_add_effects(const State& state,
                                                                                   const AtomIndexList& add_fluent_atom_indices,
                                                                                   const AtomIndexList& del_fluent_atom_indices) const
{
    return mutable_novelty_table().test_novelty_read_only_from_delta(state, add_fluent_atom_indices, del_fluent_atom_indices);
}

bool LandmarkNoveltyPruningStrategyImpl::supports_transition_novel_witness_query() const
{
    return m_novelty_table.get_tuple_index_mapper().get_arity() == 1;
}

void LandmarkNoveltyPruningStrategyImpl::compute_transition_novel_fluent_atom_indices_read_only(const State& state,
                                                                                                 const State& succ_state,
                                                                                                 AtomIndexList& out_novel_fluent_atom_indices) const
{
    if (!supports_transition_novel_witness_query())
    {
        throw std::invalid_argument("LandmarkNoveltyPruningStrategyImpl transition witness query only supports arity 1.");
    }

    mutable_novelty_table().compute_transition_novel_fluent_atom_indices_read_only(state, succ_state, out_novel_fluent_atom_indices);
}

const LandmarkNoveltyTable& LandmarkNoveltyPruningStrategyImpl::get_novelty_table() const { return m_novelty_table; }

namespace
{
static constexpr auto NO_ABSTRACTED_POSITION = MAX_INDEX;
}

size_t AbstractedNoveltyPruningStrategyImpl::FeatureKeyHash::operator()(const FeatureKey& key) const noexcept
{
    size_t seed = 0;
    loki::hash_combine(seed, static_cast<Index>(key.m_kind));
    loki::hash_combine(seed, key.m_predicate_index);
    loki::hash_combine(seed, key.m_preserved_position);
    loki::hash_combine(seed, key.m_preserved_object_index);
    for (const auto value : key.m_signature)
    {
        loki::hash_combine(seed, value);
    }
    return seed;
}

size_t AbstractedNoveltyPruningStrategyImpl::PairKeyHash::operator()(const PairKey& key) const noexcept
{
    size_t seed = 0;
    loki::hash_combine(seed, key.m_a);
    loki::hash_combine(seed, key.m_b);
    return seed;
}

size_t AbstractedNoveltyPruningStrategyImpl::TripleKeyHash::operator()(const TripleKey& key) const noexcept
{
    size_t seed = 0;
    loki::hash_combine(seed, key.m_a);
    loki::hash_combine(seed, key.m_b);
    loki::hash_combine(seed, key.m_c);
    return seed;
}

size_t AbstractedNoveltyPruningStrategyImpl::AtomIndexListHash::operator()(const AtomIndexList& atom_indices) const noexcept
{
    size_t seed = 0;
    for (const auto atom_index : atom_indices)
    {
        loki::hash_combine(seed, atom_index);
    }
    return seed;
}

AbstractedNoveltyPruningStrategyImpl::NoveltyTables::NoveltyTables(size_t width) :
    m_width(width),
    m_dense_singletons(false),
    m_seen_singletons(),
    m_delta_singletons(),
    m_seen_singletons_dense(),
    m_delta_singletons_dense(),
    m_delta_singleton_touched(),
    m_seen_pairs(),
    m_delta_pairs(),
    m_seen_triples(),
    m_delta_triples()
{
}

void AbstractedNoveltyPruningStrategyImpl::NoveltyTables::ensure_dense_singleton_capacity(FeatureId feature)
{
    const auto required_size = static_cast<size_t>(feature) + 1;
    if (required_size <= m_seen_singletons_dense.size())
    {
        return;
    }
    m_seen_singletons_dense.resize(required_size, 0);
    m_delta_singletons_dense.resize(required_size, 0);
}

bool AbstractedNoveltyPruningStrategyImpl::NoveltyTables::contains_single(FeatureId feature) const
{
    if (m_dense_singletons)
    {
        return feature < m_seen_singletons_dense.size() && m_seen_singletons_dense[feature] != 0;
    }
    return m_seen_singletons.contains(feature);
}

bool AbstractedNoveltyPruningStrategyImpl::NoveltyTables::contains_pair(PairKey pair) const { return m_seen_pairs.contains(pair); }

bool AbstractedNoveltyPruningStrategyImpl::NoveltyTables::contains_triple(TripleKey triple) const { return m_seen_triples.contains(triple); }

bool AbstractedNoveltyPruningStrategyImpl::NoveltyTables::contains_single_or_delta(FeatureId feature) const
{
    if (m_dense_singletons)
    {
        return contains_single(feature) || (feature < m_delta_singletons_dense.size() && m_delta_singletons_dense[feature] != 0);
    }
    return m_seen_singletons.contains(feature) || m_delta_singletons.contains(feature);
}

bool AbstractedNoveltyPruningStrategyImpl::NoveltyTables::contains_pair_or_delta(PairKey pair) const
{
    return m_seen_pairs.contains(pair) || m_delta_pairs.contains(pair);
}

bool AbstractedNoveltyPruningStrategyImpl::NoveltyTables::contains_triple_or_delta(TripleKey triple) const
{
    return m_seen_triples.contains(triple) || m_delta_triples.contains(triple);
}

bool AbstractedNoveltyPruningStrategyImpl::NoveltyTables::insert_single(FeatureId feature)
{
    if (!m_dense_singletons)
    {
        return m_seen_singletons.emplace(feature).second;
    }
    ensure_dense_singleton_capacity(feature);
    if (m_seen_singletons_dense[feature] != 0)
    {
        return false;
    }
    m_seen_singletons_dense[feature] = 1;
    return true;
}

bool AbstractedNoveltyPruningStrategyImpl::NoveltyTables::insert_pair(PairKey pair)
{
    return m_width >= 2 && m_seen_pairs.emplace(pair).second;
}

bool AbstractedNoveltyPruningStrategyImpl::NoveltyTables::insert_triple(TripleKey triple)
{
    return m_width >= 3 && m_seen_triples.emplace(triple).second;
}

bool AbstractedNoveltyPruningStrategyImpl::NoveltyTables::insert_delta_single(FeatureId feature)
{
    if (!m_dense_singletons)
    {
        return m_delta_singletons.emplace(feature).second;
    }
    ensure_dense_singleton_capacity(feature);
    if (m_delta_singletons_dense[feature] != 0)
    {
        return false;
    }
    m_delta_singletons_dense[feature] = 1;
    m_delta_singleton_touched.push_back(feature);
    return true;
}

bool AbstractedNoveltyPruningStrategyImpl::NoveltyTables::insert_delta_pair(PairKey pair)
{
    return m_width >= 2 && m_delta_pairs.emplace(pair).second;
}

bool AbstractedNoveltyPruningStrategyImpl::NoveltyTables::insert_delta_triple(TripleKey triple)
{
    return m_width >= 3 && m_delta_triples.emplace(triple).second;
}

void AbstractedNoveltyPruningStrategyImpl::NoveltyTables::clear_delta()
{
    if (m_dense_singletons)
    {
        for (const auto feature : m_delta_singleton_touched)
        {
            m_delta_singletons_dense[feature] = 0;
        }
        m_delta_singleton_touched.clear();
    }
    else
    {
        m_delta_singletons.clear();
    }
    m_delta_pairs.clear();
    m_delta_triples.clear();
}

void AbstractedNoveltyPruningStrategyImpl::NoveltyTables::reserve_singletons(size_t count)
{
    m_dense_singletons = true;
    m_seen_singletons_dense.assign(count, 0);
    m_delta_singletons_dense.assign(count, 0);
    m_delta_singleton_touched.reserve(count);
}

void AbstractedNoveltyPruningStrategyImpl::NoveltyTables::commit_delta()
{
    if (m_dense_singletons)
    {
        for (const auto feature : m_delta_singleton_touched)
        {
            insert_single(feature);
        }
    }
    else
    {
        m_seen_singletons.insert(m_delta_singletons.begin(), m_delta_singletons.end());
    }
    m_seen_pairs.insert(m_delta_pairs.begin(), m_delta_pairs.end());
    m_seen_triples.insert(m_delta_triples.begin(), m_delta_triples.end());
    clear_delta();
}

AbstractedNoveltyPruningStrategyImpl::AbstractedNoveltyPruningStrategyImpl(formalism::Problem problem,
                                                                           size_t width,
                                                                           bool base_abstracted,
                                                                           bool preserve_goal_atoms,
                                                                           bool keep_depth_one_novel,
                                                                           landmarks::FactLandmarkGraph landmarks,
                                                                           LandmarkGrouping grouping) :
    m_problem(std::move(problem)),
    m_width(width),
    m_base_abstracted(base_abstracted),
    m_preserve_goal_atoms(preserve_goal_atoms),
    m_keep_depth_one_novel(keep_depth_one_novel),
    m_root_state_index(std::nullopt),
    m_feature_ids(),
    m_features_by_atom_index(),
    m_goal_fluent_atom_indices(),
    m_skip_depth_one_expansion_state_indices(),
    m_skip_depth_one_expansion_fluent_atom_indices_fallback(),
    m_tables_by_rank(),
    m_active_tables(nullptr),
    m_landmark_coordinates(std::nullopt)
{
    if (m_width < 1 || m_width > 3)
    {
        throw std::invalid_argument("AbstractedNoveltyPruningStrategyImpl: width must be in {1, 2, 3}.");
    }

    if (landmarks)
    {
        /* Abstracted LIW(k). The coordinate is built exactly as the non-abstracted strategy builds
           it -- same atoms, same grouping -- because the landmark half of the feature is the same
           question; only the tuple half is abstracted. */
        auto landmark_atom_indices = AtomIndexList(landmarks->get_landmark_atom_indices().begin(), landmarks->get_landmark_atom_indices().end());
        m_landmark_coordinates.emplace(std::move(landmark_atom_indices),
                                       grouping.disjunctive_landmarks,
                                       grouping.unshared_atom_indices,
                                       grouping.mode);
        m_tables_by_rank.reserve(m_landmark_coordinates->get_num_ranks());
        for (size_t rank = 0; rank < m_landmark_coordinates->get_num_ranks(); ++rank)
        {
            m_tables_by_rank.emplace_back(width);
        }
        m_rank_reserved.assign(m_tables_by_rank.size(), 0);
    }
    else
    {
        m_tables_by_rank.emplace_back(width);
        m_rank_reserved.assign(1, 0);
    }
    m_active_tables = &m_tables_by_rank.front();

    precompute_goal_atom_indices();
    precompute_atom_features();
}

void AbstractedNoveltyPruningStrategyImpl::activate_rank(uint32_t rank) const
{
    m_active_tables = &m_tables_by_rank[rank];
    if (m_width == 1 && !m_rank_reserved[rank])
    {
        m_rank_reserved[rank] = 1;
        m_active_tables->reserve_singletons(m_feature_ids.size());
    }
}

void AbstractedNoveltyPruningStrategyImpl::collect_transition_ranks(const State& state, const State& succ_state) const
{
    m_landmark_coordinates->collect_transition(state, succ_state, m_scratch_flipped_ranks, m_scratch_kept_ranks);
}

void AbstractedNoveltyPruningStrategyImpl::refresh_delta_query_state(const State& state) const
{
    if (m_delta_query_state_index.has_value() && (*m_delta_query_state_index == state.get_index()))
    {
        return;
    }
    m_landmark_coordinates->collect_true_landmark_atoms(state, m_scratch_true_landmark_atoms);
    m_landmark_coordinates->collect_rank_carrier_counts(m_scratch_true_landmark_atoms, m_scratch_rank_carrier_counts);
    m_delta_query_state_index = state.get_index();
}

PruningStrategy AbstractedNoveltyPruningStrategyImpl::create(formalism::Problem problem,
                                                             size_t width,
                                                             bool base_abstracted,
                                                             bool preserve_goal_atoms,
                                                             bool keep_depth_one_novel,
                                                             landmarks::FactLandmarkGraph landmarks,
                                                             LandmarkGrouping grouping)
{
    return std::make_shared<AbstractedNoveltyPruningStrategyImpl>(std::move(problem),
                                                                  width,
                                                                  base_abstracted,
                                                                  preserve_goal_atoms,
                                                                  keep_depth_one_novel,
                                                                  std::move(landmarks),
                                                                  std::move(grouping));
}

void AbstractedNoveltyPruningStrategyImpl::precompute_goal_atom_indices()
{
    if (!m_preserve_goal_atoms)
    {
        return;
    }
    const auto goal_atoms = m_problem->get_goal_atoms<PositiveTag, FluentTag>();
    m_goal_fluent_atom_indices.reserve(goal_atoms.size());
    for (const auto atom : goal_atoms)
    {
        m_goal_fluent_atom_indices.emplace(atom->get_index());
    }
}

void AbstractedNoveltyPruningStrategyImpl::precompute_atom_features()
{
    const auto ground_atoms = m_problem->get_repositories().get_ground_atoms<FluentTag>();
    auto max_atom_index = Index(0);
    auto has_ground_atoms = false;
    auto estimated_feature_count = size_t(0);
    for (const auto& ground_atom : ground_atoms)
    {
        max_atom_index = std::max(max_atom_index, ground_atom.get_index());
        has_ground_atoms = true;
        estimated_feature_count += ground_atom.get_arity() == 0 ? 1 : ground_atom.get_arity();
    }
    m_features_by_atom_index.resize(has_ground_atoms ? (max_atom_index + 1) : 0);
    m_feature_ids.reserve(estimated_feature_count);
    for (const auto& ground_atom : ground_atoms)
    {
        m_features_by_atom_index[ground_atom.get_index()] = compute_features_for_atom(&ground_atom);
    }
    if (m_width == 1)
    {
        /* Rank 0 only. `activate_rank` reserves the others on first use, so a landmark graph whose
           ranks the search never reaches does not pay `num_ranks x num_features` bytes for them. */
        m_rank_reserved[0] = 1;
        m_tables_by_rank.front().reserve_singletons(m_feature_ids.size());
    }
}

void AbstractedNoveltyPruningStrategyImpl::ensure_atom_feature_capacity(AtomIndex atom_index) const
{
    if (atom_index >= m_features_by_atom_index.size())
    {
        m_features_by_atom_index.resize(atom_index + 1);
    }
}

const std::vector<AbstractedNoveltyPruningStrategyImpl::FeatureId>& AbstractedNoveltyPruningStrategyImpl::get_atom_features(AtomIndex atom_index) const
{
    ensure_atom_feature_capacity(atom_index);
    auto& features = m_features_by_atom_index[atom_index];
    if (features.empty())
    {
        features = compute_features_for_atom(m_problem->get_repositories().get_ground_atom<FluentTag>(atom_index));
    }
    return features;
}

AbstractedNoveltyPruningStrategyImpl::FeatureId AbstractedNoveltyPruningStrategyImpl::intern_feature(const FeatureKey& key) const
{
    const auto [it, inserted] = m_feature_ids.emplace(key, static_cast<FeatureId>(m_feature_ids.size()));
    [[maybe_unused]] const auto ignored_inserted = inserted;
    return it->second;
}

std::vector<AbstractedNoveltyPruningStrategyImpl::FeatureId>
AbstractedNoveltyPruningStrategyImpl::compute_features_for_atom(formalism::GroundAtom<FluentTag> atom) const
{
    const auto& objects = atom->get_objects();

    auto features = std::vector<FeatureId> {};
    if (m_preserve_goal_atoms && m_goal_fluent_atom_indices.contains(atom->get_index()))
    {
        features.push_back(intern_feature(make_full_atom_key(atom)));
        if (objects.size() <= 1)
        {
            return features;
        }
    }

    if (objects.empty())
    {
        return { intern_feature(make_full_atom_key(atom)) };
    }

    features.reserve(features.size() + objects.size());
    for (auto position = Index(0); position < objects.size(); ++position)
    {
        features.push_back(intern_feature(make_abstracted_key(atom, position)));
    }
    return features;
}

AbstractedNoveltyPruningStrategyImpl::FeatureKey
AbstractedNoveltyPruningStrategyImpl::make_full_atom_key(formalism::GroundAtom<FluentTag> atom) const
{
    auto key = FeatureKey { FeatureKind::FULL_ATOM, atom->get_predicate()->get_index(), NO_ABSTRACTED_POSITION, NO_ABSTRACTED_POSITION, IndexList {} };
    key.m_signature.reserve(atom->get_objects().size());
    for (const auto object : atom->get_objects())
    {
        key.m_signature.push_back(object->get_index());
    }
    return key;
}

AbstractedNoveltyPruningStrategyImpl::FeatureKey
AbstractedNoveltyPruningStrategyImpl::make_abstracted_key(formalism::GroundAtom<FluentTag> atom, Index preserved_position) const
{
    const auto& objects = atom->get_objects();
    auto key = FeatureKey { FeatureKind::ABSTRACTED,
                            atom->get_predicate()->get_index(),
                            preserved_position,
                            objects[preserved_position]->get_index(),
                            IndexList {} };
    if (m_base_abstracted)
    {
        return key;
    }
    key.m_signature.reserve(objects.size() * 2);
    for (auto position = Index(0); position < objects.size(); ++position)
    {
        if (position == preserved_position)
        {
            continue;
        }
        append_object_type_signature(objects[position], key.m_signature);
    }
    return key;
}

void AbstractedNoveltyPruningStrategyImpl::append_object_type_signature(formalism::Object object, IndexList& out) const
{
    const auto& bases = object->get_bases();
    if (bases.empty())
    {
        out.push_back(1);
        out.push_back(MAX_INDEX);
        return;
    }
    out.push_back(static_cast<Index>(bases.size()));
    for (const auto type : bases)
    {
        out.push_back(type->get_index());
    }
}

void AbstractedNoveltyPruningStrategyImpl::state_groups(const State& state, std::vector<AtomFeatureGroup>& out_groups) const
{
    out_groups.clear();
    const auto& atoms = state.get_atoms<FluentTag>();
    auto has_atoms = false;
    auto atom_count = size_t(0);
    auto max_atom_index = AtomIndex(0);
    for (const auto atom_index : atoms)
    {
        const auto fluent_atom_index = static_cast<AtomIndex>(atom_index);
        max_atom_index = std::max(max_atom_index, fluent_atom_index);
        has_atoms = true;
        ++atom_count;
    }
    if (has_atoms)
    {
        ensure_atom_feature_capacity(max_atom_index);
    }
    out_groups.reserve(atom_count);
    for (const auto atom_index : atoms)
    {
        const auto fluent_atom_index = static_cast<AtomIndex>(atom_index);
        out_groups.push_back(AtomFeatureGroup { fluent_atom_index, false, &get_atom_features(fluent_atom_index) });
    }
}

std::vector<AbstractedNoveltyPruningStrategyImpl::AtomFeatureGroup> AbstractedNoveltyPruningStrategyImpl::state_groups(const State& state) const
{
    auto groups = std::vector<AtomFeatureGroup> {};
    state_groups(state, groups);
    return groups;
}

void AbstractedNoveltyPruningStrategyImpl::successor_groups(const State& state,
                                                            const AtomIndexList& succ_fluent_atom_indices,
                                                            std::vector<AtomFeatureGroup>& out_groups) const
{
    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    out_groups.clear();
    auto has_atoms = false;
    auto max_atom_index = AtomIndex(0);
    for (const auto atom_index : succ_fluent_atom_indices)
    {
        max_atom_index = std::max(max_atom_index, atom_index);
        has_atoms = true;
    }
    if (has_atoms)
    {
        ensure_atom_feature_capacity(max_atom_index);
    }
    out_groups.reserve(succ_fluent_atom_indices.size());
    for (const auto atom_index : succ_fluent_atom_indices)
    {
        out_groups.push_back(AtomFeatureGroup { atom_index, !state_fluent_atoms.get(atom_index), &get_atom_features(atom_index) });
    }
}

std::vector<AbstractedNoveltyPruningStrategyImpl::AtomFeatureGroup>
AbstractedNoveltyPruningStrategyImpl::successor_groups(const State& state, const AtomIndexList& succ_fluent_atom_indices) const
{
    auto groups = std::vector<AtomFeatureGroup> {};
    successor_groups(state, succ_fluent_atom_indices, groups);
    return groups;
}

void AbstractedNoveltyPruningStrategyImpl::atom_indices_key(const State& state, AtomIndexList& out_atom_indices) const
{
    out_atom_indices.assign(state.get_atoms<FluentTag>().begin(), state.get_atoms<FluentTag>().end());
}

AtomIndexList AbstractedNoveltyPruningStrategyImpl::atom_indices_key(const State& state) const
{
    auto atom_indices = AtomIndexList {};
    atom_indices_key(state, atom_indices);
    return atom_indices;
}

bool AbstractedNoveltyPruningStrategyImpl::test_atom_novelty(AtomIndex atom_index) const
{
    for (const auto feature : get_atom_features(atom_index))
    {
        if (!tables().contains_single(feature))
        {
            return true;
        }
    }
    return false;
}

bool AbstractedNoveltyPruningStrategyImpl::test_atom_novelty_and_update_table(AtomIndex atom_index)
{
    auto is_novel = false;
    for (const auto feature : get_atom_features(atom_index))
    {
        if (tables().insert_single(feature))
        {
            is_novel = true;
        }
    }
    return is_novel;
}

bool AbstractedNoveltyPruningStrategyImpl::test_atom_novelty_and_update_delta(AtomIndex atom_index)
{
    auto is_novel = false;
    for (const auto feature : get_atom_features(atom_index))
    {
        if (!tables().contains_single(feature) && tables().insert_delta_single(feature))
        {
            is_novel = true;
        }
    }
    return is_novel;
}

bool AbstractedNoveltyPruningStrategyImpl::test_state_novelty_and_update_table(const State& state)
{
    if (m_width == 1)
    {
        auto is_novel = false;
        for (const auto atom_index : state.get_atoms<FluentTag>())
        {
            if (test_atom_novelty_and_update_table(atom_index))
            {
                is_novel = true;
            }
        }
        return is_novel;
    }

    state_groups(state, m_scratch_atom_feature_groups);
    for (auto& group : m_scratch_atom_feature_groups)
    {
        group.m_added = true;
    }
    generate_tuples(m_scratch_atom_feature_groups, false, m_scratch_generated_tuples);
    const auto& tuples = m_scratch_generated_tuples;
    const auto is_novel = !tuples.m_singles.empty() || !tuples.m_pairs.empty() || !tuples.m_triples.empty();
    if (is_novel)
    {
        insert_tuples(tuples);
    }
    return is_novel;
}

bool AbstractedNoveltyPruningStrategyImpl::test_transition_novelty(const State& state, const State& succ_state) const
{
    if (m_width != 1)
    {
        atom_indices_key(succ_state, m_scratch_atom_indices_key);
        return test_transition_novelty(state, m_scratch_atom_indices_key);
    }

    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    const auto& succ_state_fluent_atoms = succ_state.get_atoms<FluentTag>();

    auto it_state = state_fluent_atoms.begin();
    auto it_succ_state = succ_state_fluent_atoms.begin();
    while (it_state != state_fluent_atoms.end() && it_succ_state != succ_state_fluent_atoms.end())
    {
        if (*it_succ_state < *it_state)
        {
            if (test_atom_novelty(*it_succ_state))
            {
                return true;
            }
            ++it_succ_state;
        }
        else if (*it_state < *it_succ_state)
        {
            ++it_state;
        }
        else
        {
            ++it_state;
            ++it_succ_state;
        }
    }

    for (; it_succ_state != succ_state_fluent_atoms.end(); ++it_succ_state)
    {
        if (test_atom_novelty(*it_succ_state))
        {
            return true;
        }
    }
    return false;
}

bool AbstractedNoveltyPruningStrategyImpl::test_transition_novelty(const State& state, const AtomIndexList& succ_fluent_atom_indices) const
{
    if (m_width == 1)
    {
        const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
        for (const auto atom_index : succ_fluent_atom_indices)
        {
            if (!state_fluent_atoms.get(atom_index) && test_atom_novelty(atom_index))
            {
                return true;
            }
        }
        return false;
    }
    successor_groups(state, succ_fluent_atom_indices, m_scratch_atom_feature_groups);
    generate_tuples(m_scratch_atom_feature_groups, false, m_scratch_generated_tuples);
    const auto& tuples = m_scratch_generated_tuples;
    return !tuples.m_singles.empty() || !tuples.m_pairs.empty() || !tuples.m_triples.empty();
}

bool AbstractedNoveltyPruningStrategyImpl::test_transition_novelty_and_update_table(const State& state, const State& succ_state)
{
    if (m_width != 1)
    {
        atom_indices_key(succ_state, m_scratch_atom_indices_key);
        return test_transition_novelty_and_update_table(state, m_scratch_atom_indices_key);
    }

    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    const auto& succ_state_fluent_atoms = succ_state.get_atoms<FluentTag>();

    auto is_novel = false;
    auto it_state = state_fluent_atoms.begin();
    auto it_succ_state = succ_state_fluent_atoms.begin();
    while (it_state != state_fluent_atoms.end() && it_succ_state != succ_state_fluent_atoms.end())
    {
        if (*it_succ_state < *it_state)
        {
            if (test_atom_novelty_and_update_table(*it_succ_state))
            {
                is_novel = true;
            }
            ++it_succ_state;
        }
        else if (*it_state < *it_succ_state)
        {
            ++it_state;
        }
        else
        {
            ++it_state;
            ++it_succ_state;
        }
    }

    for (; it_succ_state != succ_state_fluent_atoms.end(); ++it_succ_state)
    {
        if (test_atom_novelty_and_update_table(*it_succ_state))
        {
            is_novel = true;
        }
    }
    return is_novel;
}

bool AbstractedNoveltyPruningStrategyImpl::test_transition_novelty_and_update_table(const State& state, const AtomIndexList& succ_fluent_atom_indices)
{
    return test_transition_and_update(state, succ_fluent_atom_indices, false);
}

bool AbstractedNoveltyPruningStrategyImpl::test_transition_novelty_and_update_delta(const State& state, const State& succ_state)
{
    if (m_width != 1)
    {
        atom_indices_key(succ_state, m_scratch_atom_indices_key);
        return test_transition_novelty_and_update_delta(state, m_scratch_atom_indices_key);
    }

    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    const auto& succ_state_fluent_atoms = succ_state.get_atoms<FluentTag>();

    auto is_novel = false;
    auto it_state = state_fluent_atoms.begin();
    auto it_succ_state = succ_state_fluent_atoms.begin();
    while (it_state != state_fluent_atoms.end() && it_succ_state != succ_state_fluent_atoms.end())
    {
        if (*it_succ_state < *it_state)
        {
            if (test_atom_novelty_and_update_delta(*it_succ_state))
            {
                is_novel = true;
            }
            ++it_succ_state;
        }
        else if (*it_state < *it_succ_state)
        {
            ++it_state;
        }
        else
        {
            ++it_state;
            ++it_succ_state;
        }
    }

    for (; it_succ_state != succ_state_fluent_atoms.end(); ++it_succ_state)
    {
        if (test_atom_novelty_and_update_delta(*it_succ_state))
        {
            is_novel = true;
        }
    }
    return is_novel;
}

bool AbstractedNoveltyPruningStrategyImpl::test_transition_novelty_and_update_delta(const State& state, const AtomIndexList& succ_fluent_atom_indices)
{
    return test_transition_and_update(state, succ_fluent_atom_indices, true);
}

bool AbstractedNoveltyPruningStrategyImpl::test_transition_and_update(const State& state, const AtomIndexList& succ_fluent_atom_indices, bool use_delta)
{
    if (m_width == 1)
    {
        auto is_novel = false;
        const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
        for (const auto atom_index : succ_fluent_atom_indices)
        {
            if (state_fluent_atoms.get(atom_index))
            {
                continue;
            }
            const auto atom_is_novel = use_delta ? test_atom_novelty_and_update_delta(atom_index) : test_atom_novelty_and_update_table(atom_index);
            if (atom_is_novel)
            {
                is_novel = true;
            }
        }
        return is_novel;
    }

    successor_groups(state, succ_fluent_atom_indices, m_scratch_atom_feature_groups);
    generate_tuples(m_scratch_atom_feature_groups, use_delta, m_scratch_generated_tuples);
    const auto& tuples = m_scratch_generated_tuples;
    const auto is_novel = !tuples.m_singles.empty() || !tuples.m_pairs.empty() || !tuples.m_triples.empty();
    if (is_novel)
    {
        if (use_delta)
        {
            insert_delta_tuples(tuples);
        }
        else
        {
            insert_tuples(tuples);
        }
    }
    return is_novel;
}

void AbstractedNoveltyPruningStrategyImpl::generate_tuples(const std::vector<AtomFeatureGroup>& groups, bool use_delta, GeneratedTuples& out_tuples) const
{
    out_tuples.m_singles.clear();
    out_tuples.m_pairs.clear();
    out_tuples.m_triples.clear();
    auto& local_singletons = m_scratch_local_singletons;
    auto& local_pairs = m_scratch_local_pairs;
    auto& local_triples = m_scratch_local_triples;
    local_singletons.clear();
    local_pairs.clear();
    local_triples.clear();
    auto& added_group_indices = m_scratch_added_group_indices;
    added_group_indices.clear();
    added_group_indices.reserve(groups.size());

    for (size_t i = 0; i < groups.size(); ++i)
    {
        const auto& gi = groups[i];
        if (gi.m_added)
        {
            added_group_indices.push_back(i);
            for (const auto fi : *gi.m_features)
            {
                if (is_single_novel(fi, use_delta) && local_singletons.emplace(fi).second)
                {
                    out_tuples.m_singles.push_back(fi);
                }
            }
        }
    }

    if (m_width < 2 || added_group_indices.empty())
    {
        return;
    }

    auto emit_pair = [&](size_t lhs_group_index, size_t rhs_group_index)
    {
        const auto& lhs_group = groups[lhs_group_index];
        const auto& rhs_group = groups[rhs_group_index];
        for (const auto lhs_feature : *lhs_group.m_features)
        {
            for (const auto rhs_feature : *rhs_group.m_features)
            {
                if (lhs_feature == rhs_feature)
                {
                    continue;
                }
                auto pair_lhs = lhs_feature;
                auto pair_rhs = rhs_feature;
                if (pair_rhs < pair_lhs)
                {
                    std::swap(pair_lhs, pair_rhs);
                }
                const auto pair = PairKey { pair_lhs, pair_rhs };
                if (is_pair_novel(pair, use_delta) && local_pairs.emplace(pair).second)
                {
                    out_tuples.m_pairs.push_back(pair);
                }
            }
        }
    };

    // Assign each pair to its smallest added group index to skip all-old combinations.
    for (const auto added_group_index : added_group_indices)
    {
        for (size_t other_group_index = 0; other_group_index < groups.size(); ++other_group_index)
        {
            if (other_group_index == added_group_index || (other_group_index < added_group_index && groups[other_group_index].m_added))
            {
                continue;
            }
            emit_pair(added_group_index, other_group_index);
        }
    }

    if (m_width < 3)
    {
        return;
    }

    auto emit_triple = [&](size_t first_group_index, size_t second_group_index, size_t third_group_index)
    {
        const auto& first_group = groups[first_group_index];
        const auto& second_group = groups[second_group_index];
        const auto& third_group = groups[third_group_index];
        for (const auto first_feature : *first_group.m_features)
        {
            for (const auto second_feature : *second_group.m_features)
            {
                for (const auto third_feature : *third_group.m_features)
                {
                    auto triple_values = std::array<FeatureId, 3> { first_feature, second_feature, third_feature };
                    std::ranges::sort(triple_values);
                    if (triple_values[0] == triple_values[1] || triple_values[1] == triple_values[2])
                    {
                        continue;
                    }
                    const auto triple = TripleKey { triple_values[0], triple_values[1], triple_values[2] };
                    if (is_triple_novel(triple, use_delta) && local_triples.emplace(triple).second)
                    {
                        out_tuples.m_triples.push_back(triple);
                    }
                }
            }
        }
    };

    // Assign each triple to its smallest added group index to avoid scanning all-old triples.
    for (const auto added_group_index : added_group_indices)
    {
        for (size_t second_group_index = 0; second_group_index < groups.size(); ++second_group_index)
        {
            if (second_group_index == added_group_index
                || (second_group_index < added_group_index && groups[second_group_index].m_added))
            {
                continue;
            }
            for (size_t third_group_index = second_group_index + 1; third_group_index < groups.size(); ++third_group_index)
            {
                if (third_group_index == added_group_index
                    || (third_group_index < added_group_index && groups[third_group_index].m_added))
                {
                    continue;
                }
                emit_triple(added_group_index, second_group_index, third_group_index);
            }
        }
    }
}

AbstractedNoveltyPruningStrategyImpl::GeneratedTuples
AbstractedNoveltyPruningStrategyImpl::generate_tuples(const std::vector<AtomFeatureGroup>& groups, bool use_delta) const
{
    auto tuples = GeneratedTuples {};
    generate_tuples(groups, use_delta, tuples);
    return tuples;
}

void AbstractedNoveltyPruningStrategyImpl::insert_tuples(const GeneratedTuples& tuples)
{
    for (const auto feature : tuples.m_singles)
    {
        tables().insert_single(feature);
    }
    for (const auto pair : tuples.m_pairs)
    {
        tables().insert_pair(pair);
    }
    for (const auto triple : tuples.m_triples)
    {
        tables().insert_triple(triple);
    }
}

void AbstractedNoveltyPruningStrategyImpl::insert_delta_tuples(const GeneratedTuples& tuples)
{
    for (const auto feature : tuples.m_singles)
    {
        tables().insert_delta_single(feature);
    }
    for (const auto pair : tuples.m_pairs)
    {
        tables().insert_delta_pair(pair);
    }
    for (const auto triple : tuples.m_triples)
    {
        tables().insert_delta_triple(triple);
    }
}

bool AbstractedNoveltyPruningStrategyImpl::is_single_novel(FeatureId feature, bool use_delta) const
{
    return use_delta ? !tables().contains_single_or_delta(feature) : !tables().contains_single(feature);
}

bool AbstractedNoveltyPruningStrategyImpl::is_pair_novel(PairKey pair, bool use_delta) const
{
    return use_delta ? !tables().contains_pair_or_delta(pair) : !tables().contains_pair(pair);
}

bool AbstractedNoveltyPruningStrategyImpl::is_triple_novel(TripleKey triple, bool use_delta) const
{
    return use_delta ? !tables().contains_triple_or_delta(triple) : !tables().contains_triple(triple);
}

bool AbstractedNoveltyPruningStrategyImpl::maybe_prune_depth_one_successor(const State& state, const State& succ_state, bool is_novel)
{
    if (m_root_state_index.has_value() && (state.get_index() == *m_root_state_index))
    {
        if (!is_novel && !m_keep_depth_one_novel)
        {
            m_skip_depth_one_expansion_state_indices.emplace(succ_state.get_index());
        }
        return false;
    }
    return !is_novel;
}

bool AbstractedNoveltyPruningStrategyImpl::maybe_prune_staged_depth_one_successor(const State& state,
                                                                                   const AtomIndexList& succ_fluent_atom_indices,
                                                                                   bool is_novel)
{
    if (m_root_state_index.has_value() && (state.get_index() == *m_root_state_index))
    {
        if (!is_novel && !m_keep_depth_one_novel)
        {
            m_skip_depth_one_expansion_fluent_atom_indices_fallback.emplace(succ_fluent_atom_indices.begin(), succ_fluent_atom_indices.end());
        }
        return false;
    }
    return !is_novel;
}

bool AbstractedNoveltyPruningStrategyImpl::test_landmark_transition_width_one(const State& state, const State& succ_state, bool update)
{
    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    auto is_novel = false;

    for (const auto atom_index : succ_state.get_atoms<FluentTag>())
    {
        const auto& features = get_atom_features(atom_index);
        if (features.empty())
        {
            continue;
        }
        const auto is_added = !state_fluent_atoms.get(atom_index);

        /* A flipped rank pairs with EVERY tuple of the successor, so every atom is offered to it;
           a kept rank only with tuples containing an added atom. */
        const auto probe = [&](const std::vector<uint32_t>& ranks)
        {
            for (const auto rank : ranks)
            {
                activate_rank(rank);
                for (const auto feature : features)
                {
                    /* Read-only stops at the first witness; the updating form must not, or the
                       features it skipped are re-derived as novel by a later state. */
                    if (update ? tables().insert_single(feature) : !tables().contains_single(feature))
                    {
                        is_novel = true;
                        if (!update)
                        {
                            return;
                        }
                    }
                }
            }
        };

        probe(m_scratch_flipped_ranks);
        if (is_novel && !update)
        {
            return true;
        }
        if (is_added)
        {
            probe(m_scratch_kept_ranks);
            if (is_novel && !update)
            {
                return true;
            }
        }
    }
    return is_novel;
}

bool AbstractedNoveltyPruningStrategyImpl::test_prune_initial_state(const State& state)
{
    if (!m_root_state_index)
    {
        m_root_state_index = state.get_index();
    }
    if (has_landmark_coordinate())
    {
        /* Every coordinate of the initial state is "flipped on" relative to nothing, so each takes
           all of the state's tuples -- which is what the plain state test already computes. */
        m_landmark_coordinates->collect(state, m_scratch_ranks);
        for_each_rank(m_scratch_ranks, [&] { return test_state_novelty_and_update_table(state); });
        return false;
    }
    test_state_novelty_and_update_table(state);
    return false;
}

bool AbstractedNoveltyPruningStrategyImpl::test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ)
{
    if (state == succ_state)
    {
        return true;
    }
    if (!is_new_succ)
    {
        return true;
    }
    const auto is_novel = has_landmark_coordinate() ? test_landmark_transition_novelty_and_update_table(state, succ_state)
                                                    : test_transition_novelty_and_update_table(state, succ_state);
    return maybe_prune_depth_one_successor(state, succ_state, is_novel);
}

bool AbstractedNoveltyPruningStrategyImpl::test_landmark_transition_novelty_and_update_table(const State& state, const State& succ_state)
{
    collect_transition_ranks(state, succ_state);
    if (m_width == 1)
    {
        return test_landmark_transition_width_one(state, succ_state, true);
    }
    /* Above width 1 the two halves differ in which tuples they generate, not merely in which atoms
       they offer, so they go through the two existing tuple builders: the state one enumerates all
       tuples of the successor, the transition one only those containing an added atom. */
    auto is_novel = for_each_rank(m_scratch_flipped_ranks, [&] { return test_state_novelty_and_update_table(succ_state); });
    is_novel = for_each_rank(m_scratch_kept_ranks, [&] { return test_transition_novelty_and_update_table(state, succ_state); }) || is_novel;
    return is_novel;
}

bool AbstractedNoveltyPruningStrategyImpl::supports_action_add_effect_precheck() const { return m_width == 1; }

bool AbstractedNoveltyPruningStrategyImpl::should_bypass_action_add_effect_precheck(const State& state) const
{
    return m_width == 1 && m_root_state_index.has_value() && (state.get_index() == *m_root_state_index);
}

bool AbstractedNoveltyPruningStrategyImpl::precheck_requires_delete_effects() const { return has_landmark_coordinate(); }

bool AbstractedNoveltyPruningStrategyImpl::test_transition_novelty_from_add_effects(const State& state,
                                                                                    const AtomIndexList& add_fluent_atom_indices,
                                                                                    const AtomIndexList& del_fluent_atom_indices) const
{
    if (m_width != 1 || should_bypass_action_add_effect_precheck(state))
    {
        return true;
    }

    if (has_landmark_coordinate())
    {
        /* A landmark coordinate CAN be created by a delete -- a rank goes off with its last true
           carrier -- so the deletes are read here, unlike below. */
        refresh_delta_query_state(state);
        m_landmark_coordinates->collect_transition_from_delta(m_scratch_true_landmark_atoms,
                                                              m_scratch_rank_carrier_counts,
                                                              add_fluent_atom_indices,
                                                              del_fluent_atom_indices,
                                                              m_scratch_flipped_ranks,
                                                              m_scratch_kept_ranks);
        /* A flipped rank pairs with every tuple of the successor, and answering exactly would mean
           reconstructing the successor's atoms here. The precheck may over-approximate but must
           never under-approximate -- a false prunes the transition unseen -- so a flipped rank is
           answered `true` outright. It costs little: a coordinate changes on few transitions. */
        if (!m_scratch_flipped_ranks.empty())
        {
            return true;
        }
        for (const auto rank : m_scratch_kept_ranks)
        {
            activate_rank(rank);
            for (const auto atom_index : add_fluent_atom_indices)
            {
                if (test_atom_novelty(atom_index))
                {
                    return true;
                }
            }
        }
        return false;
    }

    /* Abstracted features are still atom-level, so without a landmark coordinate deletes cannot
       create novelty here. */
    [[maybe_unused]] const auto& ignored_del_fluent_atom_indices = del_fluent_atom_indices;
    for (const auto atom_index : add_fluent_atom_indices)
    {
        if (test_atom_novelty(atom_index))
        {
            return true;
        }
    }
    return false;
}

bool AbstractedNoveltyPruningStrategyImpl::consume_skip_state_expansion(const State& state)
{
    if (!m_skip_depth_one_expansion_state_indices.empty() && m_skip_depth_one_expansion_state_indices.erase(state.get_index()) > 0)
    {
        return true;
    }
    if (m_skip_depth_one_expansion_fluent_atom_indices_fallback.empty())
    {
        return false;
    }
    atom_indices_key(state, m_scratch_atom_indices_key);
    return m_skip_depth_one_expansion_fluent_atom_indices_fallback.erase(m_scratch_atom_indices_key) > 0;
}

bool AbstractedNoveltyPruningStrategyImpl::supports_atom_novelty_query() const { return m_width == 1; }

bool AbstractedNoveltyPruningStrategyImpl::test_atom_novelty_read_only(Index atom_index) const
{
    if (m_width != 1)
    {
        throw std::invalid_argument("AbstractedNoveltyPruningStrategyImpl::test_atom_novelty_read_only only supports width 1.");
    }
    return test_atom_novelty(atom_index);
}

bool AbstractedNoveltyPruningStrategyImpl::supports_transition_novel_witness_query() const { return m_width == 1; }

void AbstractedNoveltyPruningStrategyImpl::compute_transition_novel_fluent_atom_indices_read_only(const State& state,
                                                                                                  const State& succ_state,
                                                                                                  AtomIndexList& out_novel_fluent_atom_indices) const
{
    if (m_width != 1)
    {
        throw std::invalid_argument("AbstractedNoveltyPruningStrategyImpl transition witness query only supports width 1.");
    }
    out_novel_fluent_atom_indices.clear();
    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();

    if (has_landmark_coordinate())
    {
        /* The witness is an ATOM, so only added atoms can be one: a rank that flipped on makes the
           whole successor novel but names no atom the caller could route on. */
        collect_transition_ranks(state, succ_state);
        for (const auto atom_index : succ_state.get_atoms<FluentTag>())
        {
            if (state_fluent_atoms.get(atom_index))
            {
                continue;
            }
            auto is_novel = false;
            for (const auto& ranks : { std::cref(m_scratch_flipped_ranks), std::cref(m_scratch_kept_ranks) })
            {
                for (const auto rank : ranks.get())
                {
                    activate_rank(rank);
                    if (test_atom_novelty(atom_index))
                    {
                        is_novel = true;
                        break;
                    }
                }
                if (is_novel)
                {
                    break;
                }
            }
            if (is_novel)
            {
                out_novel_fluent_atom_indices.push_back(atom_index);
            }
        }
        return;
    }

    for (const auto atom_index : succ_state.get_atoms<FluentTag>())
    {
        if (!state_fluent_atoms.get(atom_index) && test_atom_novelty(atom_index))
        {
            out_novel_fluent_atom_indices.push_back(atom_index);
        }
    }
}

bool AbstractedNoveltyPruningStrategyImpl::supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const
{
    if (has_landmark_coordinate())
    {
        /* The staged entry points are handed a raw successor bitset rather than a `State`, and a
           landmark coordinate over a staged bitset is not the query `LandmarkCoordinates` answers.
           Claiming support would score the beam under the wrong feature family, silently. */
        return false;
    }
    return beam_novelty_mode == BeamNoveltyMode::ALL_TESTED || beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY;
}

bool AbstractedNoveltyPruningStrategyImpl::supports_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const
{
    return supports_beam_novelty_mode(beam_novelty_mode);
}

bool AbstractedNoveltyPruningStrategyImpl::supports_relaxed_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const
{
    return beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY;
}

bool AbstractedNoveltyPruningStrategyImpl::test_prune_successor_state_for_beam_selection(const State& state,
                                                                                         const State& succ_state,
                                                                                         bool is_new_succ,
                                                                                         BeamNoveltyMode beam_novelty_mode)
{
    if (beam_novelty_mode == BeamNoveltyMode::ALL_TESTED)
    {
        return test_prune_successor_state(state, succ_state, is_new_succ);
    }
    if (!is_new_succ || state == succ_state)
    {
        return true;
    }
    const auto is_novel = test_transition_novelty(state, succ_state);
    return maybe_prune_depth_one_successor(state, succ_state, is_novel);
}

bool AbstractedNoveltyPruningStrategyImpl::test_prune_staged_successor_state_for_beam_selection(const State& state,
                                                                                                const FlatBitset& succ_fluent_atoms,
                                                                                                const FlatBitset& succ_derived_atoms,
                                                                                                const FlatDoubleList& succ_numeric_variables,
                                                                                                const AtomIndexList& succ_fluent_atom_indices,
                                                                                                bool is_new_succ,
                                                                                                BeamNoveltyMode beam_novelty_mode)
{
    if (is_staged_self_loop(state, succ_fluent_atoms, succ_derived_atoms, succ_numeric_variables))
    {
        return true;
    }
    if (!is_new_succ)
    {
        return true;
    }
    const auto is_novel = (beam_novelty_mode == BeamNoveltyMode::ALL_TESTED)
        ? test_transition_novelty_and_update_table(state, succ_fluent_atom_indices)
        : test_transition_novelty(state, succ_fluent_atom_indices);
    return maybe_prune_staged_depth_one_successor(state, succ_fluent_atom_indices, is_novel);
}

bool AbstractedNoveltyPruningStrategyImpl::test_prune_staged_successor_state_for_relaxed_beam_selection(const State& state,
                                                                                                        const FlatBitset& succ_fluent_atoms,
                                                                                                        const FlatBitset& succ_derived_atoms,
                                                                                                        const FlatDoubleList& succ_numeric_variables,
                                                                                                        const AtomIndexList& succ_fluent_atom_indices,
                                                                                                        BeamNoveltyMode beam_novelty_mode)
{
    if (beam_novelty_mode != BeamNoveltyMode::SURVIVORS_ONLY)
    {
        throw std::invalid_argument("AbstractedNoveltyPruningStrategyImpl only supports relaxed staged beam selection in SURVIVORS_ONLY mode.");
    }
    if (is_staged_self_loop(state, succ_fluent_atoms, succ_derived_atoms, succ_numeric_variables))
    {
        return true;
    }
    const auto is_novel = test_transition_novelty(state, succ_fluent_atom_indices);
    return maybe_prune_staged_depth_one_successor(state, succ_fluent_atom_indices, is_novel);
}

void AbstractedNoveltyPruningStrategyImpl::on_begin_beam_replay(BeamNoveltyMode beam_novelty_mode)
{
    if (beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY)
    {
        tables().clear_delta();
    }
}

bool AbstractedNoveltyPruningStrategyImpl::test_prune_successor_state_for_beam_replay(const State& state,
                                                                                       const State& succ_state,
                                                                                       bool is_new_succ,
                                                                                       BeamNoveltyMode beam_novelty_mode)
{
    if (beam_novelty_mode == BeamNoveltyMode::ALL_TESTED)
    {
        return test_prune_successor_state(state, succ_state, is_new_succ);
    }
    if (state == succ_state)
    {
        return true;
    }
    const auto is_novel = test_transition_novelty_and_update_delta(state, succ_state);
    return maybe_prune_depth_one_successor(state, succ_state, is_novel);
}

bool AbstractedNoveltyPruningStrategyImpl::test_prune_staged_successor_state_for_beam_replay(const State& state,
                                                                                              const FlatBitset& succ_fluent_atoms,
                                                                                              const FlatBitset& succ_derived_atoms,
                                                                                              const FlatDoubleList& succ_numeric_variables,
                                                                                              const AtomIndexList& succ_fluent_atom_indices,
                                                                                              bool is_new_succ,
                                                                                              BeamNoveltyMode beam_novelty_mode)
{
    if (beam_novelty_mode == BeamNoveltyMode::ALL_TESTED)
    {
        return test_prune_staged_successor_state_for_beam_selection(state,
                                                                    succ_fluent_atoms,
                                                                    succ_derived_atoms,
                                                                    succ_numeric_variables,
                                                                    succ_fluent_atom_indices,
                                                                    is_new_succ,
                                                                    beam_novelty_mode);
    }
    if (is_staged_self_loop(state, succ_fluent_atoms, succ_derived_atoms, succ_numeric_variables))
    {
        return true;
    }
    const auto is_novel = test_transition_novelty_and_update_delta(state, succ_fluent_atom_indices);
    return maybe_prune_staged_depth_one_successor(state, succ_fluent_atom_indices, is_novel);
}

void AbstractedNoveltyPruningStrategyImpl::on_end_beam_replay(BeamNoveltyMode beam_novelty_mode)
{
    if (beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY)
    {
        tables().commit_delta();
    }
}

}
