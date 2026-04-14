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

ArityKNoveltyPruningStrategyImpl::ArityKNoveltyPruningStrategyImpl(size_t arity, size_t num_atoms) :
    m_novelty_table(arity, num_atoms),
    m_beam_layer_delta_tuples(),
    m_beam_layer_delta_tuple_set(),
    m_scratch_novel_tuples()
{
}

PruningStrategy ArityKNoveltyPruningStrategyImpl::create(size_t arity, size_t num_atoms)
{
    return std::make_shared<ArityKNoveltyPruningStrategyImpl>(arity, num_atoms);
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

    return !m_novelty_table.test_novelty_and_update_table(state, succ_state);
}

bool ArityKNoveltyPruningStrategyImpl::supports_action_add_effect_precheck() const { return true; }

bool ArityKNoveltyPruningStrategyImpl::test_transition_novelty_from_add_effects(const State& state,
                                                                                 const AtomIndexList& add_fluent_atom_indices) const
{
    return m_novelty_table.test_novelty_read_only(state, add_fluent_atom_indices);
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

    return !test_transition_novelty(state, succ_state);
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

        return !m_novelty_table.test_novelty_and_update_table(state, succ_fluent_atom_indices);
    }

    if (!is_new_succ)
    {
        return true;
    }

    return !m_novelty_table.test_novelty_read_only(state, succ_fluent_atom_indices);
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

    return !test_transition_novelty_and_update_delta(state, succ_state);
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

    return !m_novelty_table.test_novelty_read_only(state, succ_fluent_atom_indices);
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
