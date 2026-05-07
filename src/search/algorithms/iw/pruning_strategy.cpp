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

size_t ProjectiveArityOneNoveltyPruningStrategyImpl::AtomIndexListHash::operator()(const AtomIndexList& atom_indices) const noexcept
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
                                                                                 const AtomIndexList& add_fluent_atom_indices) const
{
    if (should_bypass_action_add_effect_precheck(state))
    {
        return true;
    }

    return m_novelty_table.test_novelty_read_only(state, add_fluent_atom_indices);
}

bool ArityKNoveltyPruningStrategyImpl::consume_skip_state_expansion(const State& state)
{
    if (m_skip_depth_one_expansion_state_indices.erase(state.get_index()) > 0)
    {
        return true;
    }

    auto fluent_atom_indices = AtomIndexList {};
    fluent_atom_indices.assign(state.get_atoms<FluentTag>().begin(), state.get_atoms<FluentTag>().end());
    return m_skip_depth_one_expansion_fluent_atom_indices_fallback.erase(fluent_atom_indices) > 0;
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
                                                                           bool keep_depth_one_novel) :
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
    m_tables(width)
{
    if (m_width < 1 || m_width > 3)
    {
        throw std::invalid_argument("AbstractedNoveltyPruningStrategyImpl: width must be in {1, 2, 3}.");
    }
    precompute_goal_atom_indices();
    precompute_atom_features();
}

PruningStrategy AbstractedNoveltyPruningStrategyImpl::create(formalism::Problem problem,
                                                             size_t width,
                                                             bool base_abstracted,
                                                             bool preserve_goal_atoms,
                                                             bool keep_depth_one_novel)
{
    return std::make_shared<AbstractedNoveltyPruningStrategyImpl>(std::move(problem), width, base_abstracted, preserve_goal_atoms, keep_depth_one_novel);
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
        m_tables.reserve_singletons(m_feature_ids.size());
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
    if (m_preserve_goal_atoms && m_goal_fluent_atom_indices.contains(atom->get_index()))
    {
        return { intern_feature(make_full_atom_key(atom)) };
    }

    const auto& objects = atom->get_objects();
    if (objects.empty())
    {
        return { intern_feature(make_full_atom_key(atom)) };
    }

    auto features = std::vector<FeatureId> {};
    features.reserve(objects.size());
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

std::vector<AbstractedNoveltyPruningStrategyImpl::AtomFeatureGroup> AbstractedNoveltyPruningStrategyImpl::state_groups(const State& state) const
{
    auto groups = std::vector<AtomFeatureGroup> {};
    const auto& atoms = state.get_atoms<FluentTag>();
    for (const auto atom_index : atoms)
    {
        const auto fluent_atom_index = static_cast<AtomIndex>(atom_index);
        groups.push_back(AtomFeatureGroup { fluent_atom_index, false, &get_atom_features(fluent_atom_index) });
    }
    return groups;
}

std::vector<AbstractedNoveltyPruningStrategyImpl::AtomFeatureGroup>
AbstractedNoveltyPruningStrategyImpl::successor_groups(const State& state, const AtomIndexList& succ_fluent_atom_indices) const
{
    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    auto groups = std::vector<AtomFeatureGroup> {};
    groups.reserve(succ_fluent_atom_indices.size());
    for (const auto atom_index : succ_fluent_atom_indices)
    {
        groups.push_back(AtomFeatureGroup { atom_index, !state_fluent_atoms.get(atom_index), &get_atom_features(atom_index) });
    }
    return groups;
}

AtomIndexList AbstractedNoveltyPruningStrategyImpl::atom_indices_key(const State& state) const
{
    auto atom_indices = AtomIndexList {};
    atom_indices.assign(state.get_atoms<FluentTag>().begin(), state.get_atoms<FluentTag>().end());
    return atom_indices;
}

bool AbstractedNoveltyPruningStrategyImpl::test_atom_novelty(AtomIndex atom_index) const
{
    for (const auto feature : get_atom_features(atom_index))
    {
        if (!m_tables.contains_single(feature))
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
        if (m_tables.insert_single(feature))
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
        if (!m_tables.contains_single(feature) && m_tables.insert_delta_single(feature))
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

    auto groups = state_groups(state);
    for (auto& group : groups)
    {
        group.m_added = true;
    }
    const auto tuples = generate_tuples(groups, false);
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
        return test_transition_novelty(state, atom_indices_key(succ_state));
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
    const auto groups = successor_groups(state, succ_fluent_atom_indices);
    const auto tuples = generate_tuples(groups, false);
    return !tuples.m_singles.empty() || !tuples.m_pairs.empty() || !tuples.m_triples.empty();
}

bool AbstractedNoveltyPruningStrategyImpl::test_transition_novelty_and_update_table(const State& state, const State& succ_state)
{
    if (m_width != 1)
    {
        return test_transition_novelty_and_update_table(state, atom_indices_key(succ_state));
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
        return test_transition_novelty_and_update_delta(state, atom_indices_key(succ_state));
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

    const auto groups = successor_groups(state, succ_fluent_atom_indices);
    const auto tuples = generate_tuples(groups, use_delta);
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

AbstractedNoveltyPruningStrategyImpl::GeneratedTuples
AbstractedNoveltyPruningStrategyImpl::generate_tuples(const std::vector<AtomFeatureGroup>& groups, bool use_delta) const
{
    auto tuples = GeneratedTuples {};
    auto local_singletons = absl::flat_hash_set<FeatureId> {};
    auto local_pairs = absl::flat_hash_set<PairKey, PairKeyHash> {};
    auto local_triples = absl::flat_hash_set<TripleKey, TripleKeyHash> {};

    for (size_t i = 0; i < groups.size(); ++i)
    {
        const auto& gi = groups[i];
        if (gi.m_added)
        {
            for (const auto fi : *gi.m_features)
            {
                if (is_single_novel(fi, use_delta) && local_singletons.emplace(fi).second)
                {
                    tuples.m_singles.push_back(fi);
                }
            }
        }
        if (m_width < 2)
        {
            continue;
        }
        for (size_t j = i + 1; j < groups.size(); ++j)
        {
            const auto& gj = groups[j];
            if (!gi.m_added && !gj.m_added)
            {
                continue;
            }
            for (const auto fi : *gi.m_features)
            {
                for (const auto fj : *gj.m_features)
                {
                    if (fi == fj)
                    {
                        continue;
                    }
                    auto pair_lhs = fi;
                    auto pair_rhs = fj;
                    if (pair_rhs < pair_lhs)
                    {
                        std::swap(pair_lhs, pair_rhs);
                    }
                    const auto pair = PairKey { pair_lhs, pair_rhs };
                    if (is_pair_novel(pair, use_delta) && local_pairs.emplace(pair).second)
                    {
                        tuples.m_pairs.push_back(pair);
                    }
                }
            }
        }
    }

    if (m_width < 3)
    {
        return tuples;
    }

    for (size_t i = 0; i < groups.size(); ++i)
    {
        const auto& gi = groups[i];
        for (size_t j = i + 1; j < groups.size(); ++j)
        {
            const auto& gj = groups[j];
            for (size_t k = j + 1; k < groups.size(); ++k)
            {
                const auto& gk = groups[k];
                if (!gi.m_added && !gj.m_added && !gk.m_added)
                {
                    continue;
                }
                for (const auto fi : *gi.m_features)
                {
                    for (const auto fj : *gj.m_features)
                    {
                        for (const auto fk : *gk.m_features)
                        {
                            auto triple_values = std::array<FeatureId, 3> { fi, fj, fk };
                            std::ranges::sort(triple_values);
                            if (triple_values[0] == triple_values[1] || triple_values[1] == triple_values[2])
                            {
                                continue;
                            }
                            const auto triple = TripleKey { triple_values[0], triple_values[1], triple_values[2] };
                            if (is_triple_novel(triple, use_delta) && local_triples.emplace(triple).second)
                            {
                                tuples.m_triples.push_back(triple);
                            }
                        }
                    }
                }
            }
        }
    }
    return tuples;
}

void AbstractedNoveltyPruningStrategyImpl::insert_tuples(const GeneratedTuples& tuples)
{
    for (const auto feature : tuples.m_singles)
    {
        m_tables.insert_single(feature);
    }
    for (const auto pair : tuples.m_pairs)
    {
        m_tables.insert_pair(pair);
    }
    for (const auto triple : tuples.m_triples)
    {
        m_tables.insert_triple(triple);
    }
}

void AbstractedNoveltyPruningStrategyImpl::insert_delta_tuples(const GeneratedTuples& tuples)
{
    for (const auto feature : tuples.m_singles)
    {
        m_tables.insert_delta_single(feature);
    }
    for (const auto pair : tuples.m_pairs)
    {
        m_tables.insert_delta_pair(pair);
    }
    for (const auto triple : tuples.m_triples)
    {
        m_tables.insert_delta_triple(triple);
    }
}

bool AbstractedNoveltyPruningStrategyImpl::is_single_novel(FeatureId feature, bool use_delta) const
{
    return use_delta ? !m_tables.contains_single_or_delta(feature) : !m_tables.contains_single(feature);
}

bool AbstractedNoveltyPruningStrategyImpl::is_pair_novel(PairKey pair, bool use_delta) const
{
    return use_delta ? !m_tables.contains_pair_or_delta(pair) : !m_tables.contains_pair(pair);
}

bool AbstractedNoveltyPruningStrategyImpl::is_triple_novel(TripleKey triple, bool use_delta) const
{
    return use_delta ? !m_tables.contains_triple_or_delta(triple) : !m_tables.contains_triple(triple);
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

bool AbstractedNoveltyPruningStrategyImpl::test_prune_initial_state(const State& state)
{
    if (!m_root_state_index)
    {
        m_root_state_index = state.get_index();
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
    const auto is_novel = test_transition_novelty_and_update_table(state, succ_state);
    return maybe_prune_depth_one_successor(state, succ_state, is_novel);
}

bool AbstractedNoveltyPruningStrategyImpl::supports_action_add_effect_precheck() const { return m_width == 1; }

bool AbstractedNoveltyPruningStrategyImpl::should_bypass_action_add_effect_precheck(const State& state) const
{
    return m_width == 1 && m_root_state_index.has_value() && (state.get_index() == *m_root_state_index);
}

bool AbstractedNoveltyPruningStrategyImpl::test_transition_novelty_from_add_effects(const State& state,
                                                                                    const AtomIndexList& add_fluent_atom_indices) const
{
    if (m_width != 1 || should_bypass_action_add_effect_precheck(state))
    {
        return true;
    }
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
    if (m_skip_depth_one_expansion_state_indices.erase(state.get_index()) > 0)
    {
        return true;
    }
    return m_skip_depth_one_expansion_fluent_atom_indices_fallback.erase(atom_indices_key(state)) > 0;
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
        m_tables.clear_delta();
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
        m_tables.commit_delta();
    }
}

ProjectiveArityOneNoveltyPruningStrategyImpl::ProjectiveArityOneNoveltyPruningStrategyImpl(formalism::Problem problem,
                                                                                           bool typed_projection,
                                                                                           bool keep_depth_one_novel,
                                                                                           bool keep_goal_nonunary_atoms) :
    m_problem(std::move(problem)),
    m_typed_projection(typed_projection),
    m_keep_depth_one_novel(keep_depth_one_novel),
    m_keep_goal_nonunary_atoms(keep_goal_nonunary_atoms),
    m_root_state_index(std::nullopt),
    m_projected_atom_keys_by_atom_index(),
    m_seen_projected_atoms(),
    m_beam_layer_delta_projected_atoms(),
    m_beam_layer_delta_projected_atoms_set(),
    m_skip_depth_one_expansion_state_indices(),
    m_skip_depth_one_expansion_fluent_atom_indices_fallback()
{
    if (m_typed_projection && !m_problem->get_requirements()->test(loki::RequirementEnum::TYPING))
    {
        throw std::runtime_error("ProjectiveArityOneNoveltyPruningStrategyImpl: typed_projection requires the :typing requirement.");
    }

    precompute_projected_atom_keys();
}

size_t ProjectiveArityOneNoveltyPruningStrategyImpl::ProjectedAtomKeyHash::operator()(const ProjectedAtomKey& key) const noexcept
{
    size_t seed = 0;
    loki::hash_combine(seed, static_cast<Index>(key.m_kind));
    loki::hash_combine(seed, key.m_predicate_index);
    loki::hash_combine(seed, key.m_position);
    loki::hash_combine(seed, key.m_projected_object_index);
    for (const auto type_index : key.m_other_slot_type_signature)
    {
        loki::hash_combine(seed, type_index);
    }
    return seed;
}

PruningStrategy ProjectiveArityOneNoveltyPruningStrategyImpl::create(formalism::Problem problem,
                                                                     bool typed_projection,
                                                                     bool keep_depth_one_novel,
                                                                     bool keep_goal_nonunary_atoms)
{
    return std::make_shared<ProjectiveArityOneNoveltyPruningStrategyImpl>(
        std::move(problem),
        typed_projection,
        keep_depth_one_novel,
        keep_goal_nonunary_atoms);
}

void ProjectiveArityOneNoveltyPruningStrategyImpl::precompute_projected_atom_keys()
{
    const auto ground_atoms = m_problem->get_repositories().get_ground_atoms<FluentTag>();
    m_projected_atom_keys_by_atom_index.clear();

    auto max_atom_index = Index(0);
    auto has_ground_atoms = false;
    for (const auto& ground_atom : ground_atoms)
    {
        max_atom_index = std::max(max_atom_index, ground_atom.get_index());
        has_ground_atoms = true;
    }
    m_projected_atom_keys_by_atom_index.resize(has_ground_atoms ? (max_atom_index + 1) : 0);

    for (const auto& ground_atom : ground_atoms)
    {
        auto& projected_atom_keys = m_projected_atom_keys_by_atom_index[ground_atom.get_index()];
        compute_projected_atom_keys_for_atom(&ground_atom, projected_atom_keys);
    }
}

void ProjectiveArityOneNoveltyPruningStrategyImpl::compute_projected_atom_keys_for_atom(
    formalism::GroundAtom<FluentTag> ground_atom,
    std::vector<ProjectedAtomKey>& out_projected_atom_keys) const
{
    out_projected_atom_keys.clear();

    const auto atom_index = ground_atom->get_index();

    // Unary atoms are kept as-is. Higher-arity atoms are split into unary features by
    // argument position. In typed mode, the feature key additionally stores the ordered
    // type signature of the *other* arguments.
    if (ground_atom->get_arity() <= 1)
    {
        out_projected_atom_keys.push_back(ProjectedAtomKey { ProjectionKind::UNARY, atom_index, 0, 0, IndexList {} });
        return;
    }

    if (m_keep_goal_nonunary_atoms && m_problem->get_goal_atoms_bitset<PositiveTag, FluentTag>().get(atom_index))
    {
        out_projected_atom_keys.push_back(ProjectedAtomKey { ProjectionKind::UNARY, atom_index, 0, 0, IndexList {} });
    }

    const auto predicate_index = ground_atom->get_predicate()->get_index();
    const auto& objects = ground_atom->get_objects();

    for (size_t position = 0; position < objects.size(); ++position)
    {
        const auto object = objects.at(position);

        if (!m_typed_projection)
        {
            out_projected_atom_keys.push_back(ProjectedAtomKey { ProjectionKind::UNTYPED,
                                                                 predicate_index,
                                                                 static_cast<Index>(position),
                                                                 object->get_index(),
                                                                 IndexList {} });
            continue;
        }

        IndexList other_slot_type_signature;
        other_slot_type_signature.reserve(objects.size() > 0 ? objects.size() - 1 : 0);

        const auto emit_typed_keys = [&](auto&& self, size_t object_position) -> void {
            if (object_position == objects.size())
            {
                out_projected_atom_keys.push_back(ProjectedAtomKey { ProjectionKind::TYPED,
                                                                     predicate_index,
                                                                     static_cast<Index>(position),
                                                                     object->get_index(),
                                                                     other_slot_type_signature });
                return;
            }

            if (object_position == position)
            {
                self(self, object_position + 1);
                return;
            }

            const auto& object_types = objects.at(object_position)->get_bases();
            if (object_types.empty())
            {
                other_slot_type_signature.push_back(MAX_INDEX);
                self(self, object_position + 1);
                other_slot_type_signature.pop_back();
                return;
            }

            for (const auto& object_type : object_types)
            {
                other_slot_type_signature.push_back(object_type->get_index());
                self(self, object_position + 1);
                other_slot_type_signature.pop_back();
            }
        };

        emit_typed_keys(emit_typed_keys, 0);
    }
}

const std::vector<ProjectiveArityOneNoveltyPruningStrategyImpl::ProjectedAtomKey>&
ProjectiveArityOneNoveltyPruningStrategyImpl::get_projected_atom_keys(AtomIndex atom_index) const
{
    if (atom_index >= m_projected_atom_keys_by_atom_index.size())
    {
        m_projected_atom_keys_by_atom_index.resize(atom_index + 1);
    }

    auto& projected_atom_keys = m_projected_atom_keys_by_atom_index[atom_index];
    if (projected_atom_keys.empty())
    {
        compute_projected_atom_keys_for_atom(m_problem->get_repositories().get_ground_atom<FluentTag>(atom_index), projected_atom_keys);
    }

    return projected_atom_keys;
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_atom_novelty(AtomIndex atom_index) const
{
    const auto& projected_atom_keys = get_projected_atom_keys(atom_index);

    return std::ranges::any_of(projected_atom_keys,
                               [this](const auto& projected_atom) { return !m_seen_projected_atoms.count(projected_atom); });
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_atom_novelty_and_update_table(AtomIndex atom_index)
{
    const auto& projected_atom_keys = get_projected_atom_keys(atom_index);

    bool is_novel = false;
    for (const auto& projected_atom : projected_atom_keys)
    {
        if (m_seen_projected_atoms.emplace(projected_atom).second)
        {
            is_novel = true;
        }
    }
    return is_novel;
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_atom_novelty_and_update_delta(AtomIndex atom_index)
{
    const auto& projected_atom_keys = get_projected_atom_keys(atom_index);

    bool is_novel = false;
    for (const auto& projected_atom : projected_atom_keys)
    {
        if (!m_seen_projected_atoms.count(projected_atom) && m_beam_layer_delta_projected_atoms_set.emplace(projected_atom).second)
        {
            m_beam_layer_delta_projected_atoms.push_back(projected_atom);
            is_novel = true;
        }
    }
    return is_novel;
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_state_novelty_and_update_table(const State& state)
{
    const auto& fluent_atoms = state.get_atoms<FluentTag>();

    bool is_novel = false;
    for (const auto atom_index : fluent_atoms)
    {
        const auto atom_is_novel = test_atom_novelty_and_update_table(atom_index);
        if (!is_novel && atom_is_novel)
        {
            is_novel = true;
        }
    }
    return is_novel;
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_transition_novelty(const State& state, const State& succ_state) const
{
    // Width-1 novelty is tested only on atoms added by the transition, but on the
    // projected feature set rather than only on the raw successor atoms.
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

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_transition_novelty_and_update_table(const State& state, const State& succ_state)
{
    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    const auto& succ_state_fluent_atoms = succ_state.get_atoms<FluentTag>();

    bool is_novel = false;

    auto it_state = state_fluent_atoms.begin();
    auto it_succ_state = succ_state_fluent_atoms.begin();

    while (it_state != state_fluent_atoms.end() && it_succ_state != succ_state_fluent_atoms.end())
    {
        if (*it_succ_state < *it_state)
        {
            const auto atom_is_novel = test_atom_novelty_and_update_table(*it_succ_state);
            if (!is_novel && atom_is_novel)
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
        const auto atom_is_novel = test_atom_novelty_and_update_table(*it_succ_state);
        if (!is_novel && atom_is_novel)
        {
            is_novel = true;
        }
    }

    return is_novel;
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_transition_novelty_and_update_delta(const State& state, const State& succ_state)
{
    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    const auto& succ_state_fluent_atoms = succ_state.get_atoms<FluentTag>();

    bool is_novel = false;

    auto it_state = state_fluent_atoms.begin();
    auto it_succ_state = succ_state_fluent_atoms.begin();

    while (it_state != state_fluent_atoms.end() && it_succ_state != succ_state_fluent_atoms.end())
    {
        if (*it_succ_state < *it_state)
        {
            const auto atom_is_novel = test_atom_novelty_and_update_delta(*it_succ_state);
            if (!is_novel && atom_is_novel)
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
        const auto atom_is_novel = test_atom_novelty_and_update_delta(*it_succ_state);
        if (!is_novel && atom_is_novel)
        {
            is_novel = true;
        }
    }

    return is_novel;
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_prune_initial_state(const State& state)
{
    if (!m_root_state_index)
    {
        m_root_state_index = state.get_index();
    }

    return !test_state_novelty_and_update_table(state);
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ)
{
    [[maybe_unused]] const auto ignored_is_new_succ = is_new_succ;

    if (state == succ_state)
    {
        return true;
    }

    if (!is_new_succ)
    {
        // Projective width-1 novelty is still transition-based, so a duplicate successor
        // can remain novel relative to a different predecessor even though it is pruned.
        return true;
    }

    const auto is_novel = test_transition_novelty_and_update_table(state, succ_state);
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

bool ProjectiveArityOneNoveltyPruningStrategyImpl::supports_action_add_effect_precheck() const { return true; }

bool ProjectiveArityOneNoveltyPruningStrategyImpl::should_bypass_action_add_effect_precheck(const State& state) const
{
    return m_root_state_index.has_value() && (state.get_index() == *m_root_state_index);
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_transition_novelty_from_add_effects(const State& state,
                                                                                             const AtomIndexList& add_fluent_atom_indices) const
{
    if (should_bypass_action_add_effect_precheck(state))
    {
        return true;
    }

    for (const auto atom_index : add_fluent_atom_indices)
    {
        if (test_atom_novelty(atom_index))
        {
            return true;
        }
    }
    return false;
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::consume_skip_state_expansion(const State& state)
{
    if (m_skip_depth_one_expansion_state_indices.erase(state.get_index()) > 0)
    {
        return true;
    }

    auto fluent_atom_indices = AtomIndexList {};
    fluent_atom_indices.assign(state.get_atoms<FluentTag>().begin(), state.get_atoms<FluentTag>().end());
    return m_skip_depth_one_expansion_fluent_atom_indices_fallback.erase(fluent_atom_indices) > 0;
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::supports_atom_novelty_query() const { return true; }

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_atom_novelty_read_only(Index atom_index) const
{
    return test_atom_novelty(atom_index);
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::supports_transition_novel_witness_query() const { return true; }

void ProjectiveArityOneNoveltyPruningStrategyImpl::compute_transition_novel_fluent_atom_indices_read_only(
    const State& state,
    const State& succ_state,
    AtomIndexList& out_novel_fluent_atom_indices) const
{
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
            if (test_atom_novelty(*it_succ_state))
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
        if (test_atom_novelty(*it_succ_state))
        {
            out_novel_fluent_atom_indices.push_back(*it_succ_state);
        }
    }
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const
{
    return beam_novelty_mode == BeamNoveltyMode::ALL_TESTED || beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY;
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::supports_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const
{
    return supports_beam_novelty_mode(beam_novelty_mode);
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::supports_relaxed_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const
{
    return beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY;
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_prune_successor_state_for_beam_selection(const State& state,
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

    // Beam selection uses the same projective width-1 novelty test. In SURVIVORS_ONLY the
    // test is read-only here, and the kept states replay novelty updates later in beam order.
    const auto is_novel = test_transition_novelty(state, succ_state);
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

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_prune_staged_successor_state_for_beam_selection(const State& state,
                                                                                                         const FlatBitset& succ_fluent_atoms,
                                                                                                         const FlatBitset& succ_derived_atoms,
                                                                                                         const FlatDoubleList& succ_numeric_variables,
                                                                                                         const AtomIndexList& succ_fluent_atom_indices,
                                                                                                         bool is_new_succ,
                                                                                                         BeamNoveltyMode beam_novelty_mode)
{
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

        auto is_novel = false;
        const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
        auto it_state = state_fluent_atoms.begin();
        auto it_succ_state = succ_fluent_atom_indices.begin();

        while (it_state != state_fluent_atoms.end() && it_succ_state != succ_fluent_atom_indices.end())
        {
            if (*it_succ_state < *it_state)
            {
                const auto atom_is_novel = test_atom_novelty_and_update_table(*it_succ_state);
                if (!is_novel && atom_is_novel)
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

        for (; it_succ_state != succ_fluent_atom_indices.end(); ++it_succ_state)
        {
            const auto atom_is_novel = test_atom_novelty_and_update_table(*it_succ_state);
            if (!is_novel && atom_is_novel)
            {
                is_novel = true;
            }
        }

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

    if (!is_new_succ)
    {
        return true;
    }

    auto is_novel = false;
    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    auto it_state = state_fluent_atoms.begin();
    auto it_succ_state = succ_fluent_atom_indices.begin();

    while (it_state != state_fluent_atoms.end() && it_succ_state != succ_fluent_atom_indices.end())
    {
        if (*it_succ_state < *it_state)
        {
            if (test_atom_novelty(*it_succ_state))
            {
                is_novel = true;
                break;
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

    for (; !is_novel && it_succ_state != succ_fluent_atom_indices.end(); ++it_succ_state)
    {
        if (test_atom_novelty(*it_succ_state))
        {
            is_novel = true;
        }
    }

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

void ProjectiveArityOneNoveltyPruningStrategyImpl::on_begin_beam_replay(BeamNoveltyMode beam_novelty_mode)
{
    if (beam_novelty_mode != BeamNoveltyMode::SURVIVORS_ONLY)
    {
        return;
    }

    m_beam_layer_delta_projected_atoms.clear();
    m_beam_layer_delta_projected_atoms_set.clear();
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_prune_staged_successor_state_for_relaxed_beam_selection(
    const State& state,
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
        throw std::invalid_argument(
            "ProjectiveArityOneNoveltyPruningStrategyImpl only supports relaxed staged beam selection in SURVIVORS_ONLY mode.");
    }

    if (is_staged_self_loop(state, succ_fluent_atoms, succ_derived_atoms, succ_numeric_variables))
    {
        return true;
    }

    auto is_novel = false;
    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    auto it_state = state_fluent_atoms.begin();
    auto it_succ_state = succ_fluent_atom_indices.begin();

    while (it_state != state_fluent_atoms.end() && it_succ_state != succ_fluent_atom_indices.end())
    {
        if (*it_succ_state < *it_state)
        {
            if (test_atom_novelty(*it_succ_state))
            {
                is_novel = true;
                break;
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

    for (; !is_novel && it_succ_state != succ_fluent_atom_indices.end(); ++it_succ_state)
    {
        if (test_atom_novelty(*it_succ_state))
        {
            is_novel = true;
        }
    }

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

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_prune_successor_state_for_beam_replay(const State& state,
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

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_prune_staged_successor_state_for_beam_replay(const State& state,
                                                                                                      const FlatBitset& succ_fluent_atoms,
                                                                                                      const FlatBitset& succ_derived_atoms,
                                                                                                      const FlatDoubleList& succ_numeric_variables,
                                                                                                      const AtomIndexList& succ_fluent_atom_indices,
                                                                                                      bool is_new_succ,
                                                                                                      BeamNoveltyMode beam_novelty_mode)
{
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

    auto is_novel = false;
    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    auto it_state = state_fluent_atoms.begin();
    auto it_succ_state = succ_fluent_atom_indices.begin();

    while (it_state != state_fluent_atoms.end() && it_succ_state != succ_fluent_atom_indices.end())
    {
        if (*it_succ_state < *it_state)
        {
            const auto atom_is_novel = test_atom_novelty_and_update_delta(*it_succ_state);
            if (!is_novel && atom_is_novel)
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

    for (; it_succ_state != succ_fluent_atom_indices.end(); ++it_succ_state)
    {
        const auto atom_is_novel = test_atom_novelty_and_update_delta(*it_succ_state);
        if (!is_novel && atom_is_novel)
        {
            is_novel = true;
        }
    }

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

void ProjectiveArityOneNoveltyPruningStrategyImpl::on_end_beam_replay(BeamNoveltyMode beam_novelty_mode)
{
    if (beam_novelty_mode != BeamNoveltyMode::SURVIVORS_ONLY)
    {
        return;
    }

    for (const auto& projected_atom : m_beam_layer_delta_projected_atoms)
    {
        m_seen_projected_atoms.emplace(projected_atom);
    }
    m_beam_layer_delta_projected_atoms.clear();
    m_beam_layer_delta_projected_atoms_set.clear();
}
}
