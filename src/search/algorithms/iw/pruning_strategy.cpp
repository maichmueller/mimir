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

size_t ArityKNoveltyPruningStrategyImpl::AtomIndexListHash::operator()(const AtomIndexList& atom_indices) const noexcept
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
    m_generated_states(),
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
    return m_novelty_table.test_novelty(state, succ_state);
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
    if (m_generated_states.count(state.get_index()))
    {
        assert(!m_novelty_table.test_novelty_and_update_table(state));
        return true;
    }
    m_generated_states.insert(state.get_index());

    return !m_novelty_table.test_novelty_and_update_table(state);
}

bool ArityKNoveltyPruningStrategyImpl::test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ)
{
    if (state == succ_state)
    {
        return true;
    }

    if (m_generated_states.count(succ_state.get_index()))
    {
        assert(!m_novelty_table.test_novelty_and_update_table(state, succ_state));
        return true;
    }
    m_generated_states.insert(succ_state.get_index());

    return !m_novelty_table.test_novelty_and_update_table(state, succ_state);
}

bool ArityKNoveltyPruningStrategyImpl::supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const
{
    return beam_novelty_mode == BeamNoveltyMode::ALL_TESTED || beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY;
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

    if (state == succ_state)
    {
        return true;
    }

    if (m_generated_states.count(succ_state.get_index()))
    {
        return true;
    }
    m_generated_states.insert(succ_state.get_index());

    return !test_transition_novelty(state, succ_state);
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
    m_generated_states(),
    m_seen_projected_atoms(),
    m_beam_layer_delta_projected_atoms(),
    m_beam_layer_delta_projected_atoms_set(),
    m_scratch_projected_atom_keys()
{
    if (m_typed_projection && !m_problem->get_requirements()->test(loki::RequirementEnum::TYPING))
    {
        throw std::runtime_error("ProjectiveArityOneNoveltyPruningStrategyImpl: typed_projection requires the :typing requirement.");
    }
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

void ProjectiveArityOneNoveltyPruningStrategyImpl::collect_projected_atom_keys(
    AtomIndex atom_index,
    std::vector<ProjectedAtomKey>& out_projected_atom_keys) const
{
    out_projected_atom_keys.clear();

    const auto ground_atom = m_problem->get_repositories().get_ground_atom<FluentTag>(atom_index);

    // Unary atoms are kept as-is. Higher-arity atoms are split into unary features by
    // argument position, which is the projection used by projective IW(1).
    if (ground_atom->get_arity() <= 1)
    {
        out_projected_atom_keys.emplace_back(0, atom_index, 0, 0, 0);
        return;
    }

    if (m_keep_goal_nonunary_atoms && m_problem->get_goal_atoms_bitset<PositiveTag, FluentTag>().get(atom_index))
    {
        out_projected_atom_keys.emplace_back(0, atom_index, 0, 0, 0);
    }

    const auto predicate_index = ground_atom->get_predicate()->get_index();
    const auto& objects = ground_atom->get_objects();

    for (size_t position = 0; position < objects.size(); ++position)
    {
        const auto object = objects.at(position);

        if (!m_typed_projection)
        {
            out_projected_atom_keys.emplace_back(1, predicate_index, static_cast<Index>(position), object->get_index(), 0);
            continue;
        }

        const auto& object_types = object->get_bases();
        if (object_types.empty())
        {
            out_projected_atom_keys.emplace_back(2, predicate_index, static_cast<Index>(position), object->get_index(), MAX_INDEX);
            continue;
        }

        for (const auto& object_type : object_types)
        {
            out_projected_atom_keys.emplace_back(2,
                                                 predicate_index,
                                                 static_cast<Index>(position),
                                                 object->get_index(),
                                                 object_type->get_index());
        }
    }
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_atom_novelty(AtomIndex atom_index) const
{
    collect_projected_atom_keys(atom_index, m_scratch_projected_atom_keys);

    return std::ranges::any_of(m_scratch_projected_atom_keys,
                               [this](const auto& projected_atom) { return !m_seen_projected_atoms.count(projected_atom); });
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_atom_novelty_and_update_table(AtomIndex atom_index)
{
    collect_projected_atom_keys(atom_index, m_scratch_projected_atom_keys);

    bool is_novel = false;
    for (const auto& projected_atom : m_scratch_projected_atom_keys)
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
    collect_projected_atom_keys(atom_index, m_scratch_projected_atom_keys);

    bool is_novel = false;
    for (const auto& projected_atom : m_scratch_projected_atom_keys)
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

    if (m_generated_states.count(state.get_index()))
    {
        assert(!test_state_novelty_and_update_table(state));
        return true;
    }
    m_generated_states.insert(state.get_index());

    return !test_state_novelty_and_update_table(state);
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ)
{
    if (state == succ_state)
    {
        return true;
    }

    if (m_generated_states.count(succ_state.get_index()))
    {
        assert(!test_transition_novelty_and_update_table(state, succ_state));
        return true;
    }
    m_generated_states.insert(succ_state.get_index());

    const auto is_novel = test_transition_novelty_and_update_table(state, succ_state);
    if (m_keep_depth_one_novel && m_root_state_index.has_value() && (state.get_index() == *m_root_state_index))
    {
        return false;
    }

    return !is_novel;
}

bool ProjectiveArityOneNoveltyPruningStrategyImpl::supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const
{
    return beam_novelty_mode == BeamNoveltyMode::ALL_TESTED || beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY;
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

    if (state == succ_state)
    {
        return true;
    }

    if (m_generated_states.count(succ_state.get_index()))
    {
        return true;
    }
    m_generated_states.insert(succ_state.get_index());

    // Beam selection uses the same projective width-1 novelty test. In SURVIVORS_ONLY the
    // test is read-only here, and the kept states replay novelty updates later in beam order.
    const auto is_novel = test_transition_novelty(state, succ_state);
    if (m_keep_depth_one_novel && m_root_state_index.has_value() && (state.get_index() == *m_root_state_index))
    {
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
    m_scratch_projected_atom_keys.clear();
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
    if (m_keep_depth_one_novel && m_root_state_index.has_value() && (state.get_index() == *m_root_state_index))
    {
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
    m_scratch_projected_atom_keys.clear();
}
}
