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

#include "mimir/search/state_repository.hpp"

#include "mimir/common/types_cista.hpp"
#include "mimir/formalism/domain.hpp"
#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/ground_literal.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/applicability.hpp"
#include "mimir/search/axiom_evaluators/interface.hpp"
#include "mimir/search/search_context.hpp"

#include <valla/indexed_hash_set.hpp>
#include <valla/valla.hpp>

using namespace mimir::formalism;

namespace mimir::search
{

StateRepositoryImpl::StagedSuccessorScratch::StagedSuccessorScratch() = default;
StateRepositoryImpl::StagedSuccessorScratch::~StagedSuccessorScratch() = default;

ContinuousCost compute_state_metric_value(const State& state)
{
    if (state.get_problem().get_auxiliary_function_value().has_value())
    {
        return state.get_problem().get_auxiliary_function_value().value()->get_number();
    }

    return state.get_problem().get_optimization_metric().has_value() ?
               evaluate(state.get_problem().get_optimization_metric().value()->get_function_expression(),
                        state.get_problem().get_initial_function_to_value<StaticTag>(),
                        state.get_numeric_variables()) :
               0.;
}

StateRepositoryImpl::StateRepositoryImpl(AxiomEvaluator axiom_evaluator) :
    m_axiom_evaluator(std::move(axiom_evaluator)),
    m_states(),
    m_packed_states_by_index(),
    m_fluent_atom_slots(),
    m_reached_fluent_atoms(),
    m_reached_derived_atoms(),
    m_applied_positive_effect_atoms(),
    m_applied_negative_effect_atoms(),
    m_index_list(),
    m_unpacked_state_pool()
{
}

StateRepository StateRepositoryImpl::create(AxiomEvaluator axiom_evaluator) { return std::make_shared<StateRepositoryImpl>(axiom_evaluator); }

std::pair<State, ContinuousCost> StateRepositoryImpl::get_or_create_initial_state()
{
    const auto problem = m_axiom_evaluator->get_problem();
    return get_or_create_state(problem->get_fluent_initial_atoms(), problem->get_initial_function_to_value<FluentTag>());
}

static void update_reached_fluent_atoms(const FlatBitset& state_fluent_atoms, FlatBitset& ref_reached_fluent_atoms)
{
    ref_reached_fluent_atoms |= state_fluent_atoms;
}

static void update_reached_derived_atoms(const FlatBitset& state_derived_atoms, FlatBitset& ref_reached_derived_atoms)
{
    ref_reached_derived_atoms |= state_derived_atoms;
}

std::pair<State, ContinuousCost> StateRepositoryImpl::get_or_create_state(const GroundAtomList<FluentTag>& atoms,
                                                                          const FlatDoubleList& fluent_numeric_variables)
{
    auto& problem = *m_axiom_evaluator->get_problem();
    auto& index_tree_table = problem.get_index_tree_table();
    auto& double_leaf_table = problem.get_double_leaf_table();

    /* Dense state */
    auto unpacked_state = m_unpacked_state_pool.get_or_allocate(problem);
    auto& dense_fluent_atoms = unpacked_state->get_atoms<FluentTag>();
    dense_fluent_atoms.unset_all();
    auto& dense_derived_atoms = unpacked_state->get_atoms<DerivedTag>();
    dense_derived_atoms.unset_all();
    auto& dense_fluent_numeric_variables = unpacked_state->get_numeric_variables();
    /* Sparse state */
    auto state_fluent_atoms_slot = valla::Slot<Index>();
    auto state_derived_atoms_slot = valla::Slot<Index>();
    auto state_numeric_variables = valla::Slot<Index>();

    /* 2. Construct non-extended state */

    /* 2.1 Numeric state variables */
    dense_fluent_numeric_variables = fluent_numeric_variables;

    m_index_list.clear();
    valla::encode_as_unsigned_integrals(dense_fluent_numeric_variables, double_leaf_table, std::back_inserter(m_index_list));
    state_numeric_variables = valla::insert_sequence(m_index_list, index_tree_table);

    /* 2.2. Propositional state */
    for (const auto& atom : atoms)
    {
        dense_fluent_atoms.set(atom->get_index());
    }

    state_fluent_atoms_slot = valla::insert_sequence(dense_fluent_atoms, index_tree_table);

    update_reached_fluent_atoms(dense_fluent_atoms, m_reached_fluent_atoms);

    // Test whether there exists an extended state for the given non extended state.
    auto it = m_states.find(PackedStateImpl(state_fluent_atoms_slot, state_derived_atoms_slot, state_numeric_variables));
    if (it != m_states.end())
    {
        m_index_list.clear();
        valla::read_sequence(it->first.get_atoms<DerivedTag>(), index_tree_table, std::back_inserter(m_index_list));
        for (const auto index : m_index_list)
        {
            dense_derived_atoms.set(index);
        }

        auto state = State(it->second, &it->first, std::move(unpacked_state), shared_from_this());
        return { state, compute_state_metric_value(state) };
    }

    /* 3. Apply axioms to construct extended state. */
    {
        if (!m_axiom_evaluator->get_problem()->get_problem_and_domain_axioms().empty())
        {
            // Evaluate axioms
            m_axiom_evaluator->generate_and_apply_axioms(*unpacked_state);

            state_derived_atoms_slot = valla::insert_sequence(dense_derived_atoms, index_tree_table);

            update_reached_derived_atoms(dense_derived_atoms, m_reached_derived_atoms);
        }
    }

    // Cache and return the extended state.
    auto result = m_states.emplace(PackedStateImpl(state_fluent_atoms_slot, state_derived_atoms_slot, state_numeric_variables), m_states.size());
    m_packed_states_by_index.push_back(&result.first->first);
    auto state = State(result.first->second, &result.first->first, std::move(unpacked_state), shared_from_this());

    return { state, compute_state_metric_value(state) };
}

static void apply_numeric_effect(const std::pair<loki::AssignOperatorEnum, ContinuousCost>& numeric_effect, ContinuousCost& ref_value)
{
    const auto [assign_operator, value] = numeric_effect;

    assert(!std::isnan(value));
    assert(assign_operator == loki::AssignOperatorEnum::ASSIGN || !std::isnan(ref_value));

    switch (assign_operator)
    {
        case loki::AssignOperatorEnum::ASSIGN:
        {
            ref_value = value;
            break;
        }
        case loki::AssignOperatorEnum::INCREASE:
        {
            ref_value += value;
            break;
        }
        case loki::AssignOperatorEnum::DECREASE:
        {
            ref_value -= value;
            break;
        }
        case loki::AssignOperatorEnum::SCALE_UP:
        {
            ref_value *= value;
            break;
        }
        case loki::AssignOperatorEnum::SCALE_DOWN:
        {
            assert(value != 0);
            ref_value /= value;
            break;
        }
        default:
        {
            throw std::logic_error("apply_numeric_effect(numeric_effect, ref_value): Unexpected loki::AssignOperatorEnum.");
        }
    }
}

static void collect_applied_fluent_numeric_effects(const GroundNumericEffectList<FluentTag>& numeric_effects,
                                                   const FlatDoubleList& static_numeric_variables,
                                                   const FlatDoubleList& fluent_numeric_variables,
                                                   FlatDoubleList& ref_numeric_variables)
{
    assert(&fluent_numeric_variables != &ref_numeric_variables);

    for (const auto& numeric_effect : numeric_effects)
    {
        const auto index = numeric_effect->get_function()->get_index();
        if (index >= ref_numeric_variables.size())
        {
            ref_numeric_variables.resize(index + 1, UNDEFINED_CONTINUOUS_COST);
        }
        const auto assign_operator_and_value = evaluate(numeric_effect, static_numeric_variables, fluent_numeric_variables);

        apply_numeric_effect(assign_operator_and_value, ref_numeric_variables[index]);
    }
}

static void collect_applied_auxiliary_numeric_effects(const GroundNumericEffect<AuxiliaryTag>& numeric_effect,
                                                      const FlatDoubleList& static_numeric_variables,
                                                      const FlatDoubleList& fluent_numeric_variables,
                                                      ContinuousCost& ref_successor_state_metric_score)
{
    const auto assign_operator_and_value = evaluate(numeric_effect, static_numeric_variables, fluent_numeric_variables);
    assert(!std::isnan(assign_operator_and_value.second));

    apply_numeric_effect(assign_operator_and_value, ref_successor_state_metric_score);
}

static void apply_action_effects(GroundAction action,
                                 const ProblemImpl& problem,
                                 const State& state,
                                 const UnpackedStateImpl& unpacked_state,
                                 FlatBitset& ref_dense_fluent_atoms,
                                 FlatBitset& ref_negative_applied_effects,
                                 FlatBitset& ref_positive_applied_effects,
                                 FlatDoubleList& ref_fluent_numeric_variables,
                                 ContinuousCost& ref_successor_state_metric_score)
{
    const auto& const_fluent_numeric_variables = state.get_numeric_variables();
    const auto& const_static_numeric_variables = problem.get_initial_function_to_value<StaticTag>();

    for (const auto& conditional_effect : action->get_conditional_effects())
    {
        if (is_applicable(conditional_effect, unpacked_state))
        {
            insert_into_bitset(conditional_effect->get_conjunctive_effect()->get_propositional_effects<NegativeTag>(), ref_negative_applied_effects);
            insert_into_bitset(conditional_effect->get_conjunctive_effect()->get_propositional_effects<PositiveTag>(), ref_positive_applied_effects);
            collect_applied_fluent_numeric_effects(conditional_effect->get_conjunctive_effect()->get_fluent_numeric_effects(),
                                                   const_static_numeric_variables,
                                                   const_fluent_numeric_variables,
                                                   ref_fluent_numeric_variables);
            if (conditional_effect->get_conjunctive_effect()->get_auxiliary_numeric_effect().has_value())
            {
                collect_applied_auxiliary_numeric_effects(conditional_effect->get_conjunctive_effect()->get_auxiliary_numeric_effect().value(),
                                                          const_static_numeric_variables,
                                                          const_fluent_numeric_variables,
                                                          ref_successor_state_metric_score);
            }
        }
    }

    // Update propositional state atoms.
    ref_dense_fluent_atoms -= ref_negative_applied_effects;
    ref_dense_fluent_atoms |= ref_positive_applied_effects;

    // Update metric in case of a fluent one.
    if (!problem.get_domain()->get_auxiliary_function_skeleton().has_value())
    {
        ref_successor_state_metric_score =
            problem.get_optimization_metric().has_value() ?
                evaluate(problem.get_optimization_metric().value()->get_function_expression(), const_static_numeric_variables, ref_fluent_numeric_variables) :
                ref_successor_state_metric_score + 1;
    }
}

std::pair<State, ContinuousCost> StateRepositoryImpl::get_or_create_successor_state(const State& state, GroundAction action, ContinuousCost state_metric_value)
{
    auto& problem = *m_axiom_evaluator->get_problem();
    auto& index_tree_table = problem.get_index_tree_table();
    auto& double_leaf_table = problem.get_double_leaf_table();

    /* Dense state*/
    auto unpacked_state = m_unpacked_state_pool.get_or_allocate(problem);
    auto& dense_fluent_atoms = unpacked_state->get_atoms<FluentTag>();
    dense_fluent_atoms = state.get_unpacked_state().get_atoms<FluentTag>();
    auto& dense_derived_atoms = unpacked_state->get_atoms<DerivedTag>();
    dense_derived_atoms = state.get_unpacked_state().get_atoms<DerivedTag>();
    auto& dense_fluent_numeric_variables = unpacked_state->get_numeric_variables();
    dense_fluent_numeric_variables = state.get_unpacked_state().get_numeric_variables();
    /* Temporaries */
    m_applied_negative_effect_atoms.unset_all();
    m_applied_positive_effect_atoms.unset_all();
    /* Sparse state */
    auto state_fluent_atoms_slot = valla::Slot<Index>();
    auto state_derived_atoms_slot = valla::Slot<Index>();
    auto state_numeric_variables = valla::Slot<Index>();

    auto successor_state_metric_value = state_metric_value;

    /* 2. Apply action effects to construct non-extended state. */

    apply_action_effects(action,
                         problem,
                         state,
                         *unpacked_state,
                         dense_fluent_atoms,
                         m_applied_negative_effect_atoms,
                         m_applied_positive_effect_atoms,
                         dense_fluent_numeric_variables,
                         successor_state_metric_value);

    state_fluent_atoms_slot = valla::insert_sequence(dense_fluent_atoms, index_tree_table);

    update_reached_fluent_atoms(dense_fluent_atoms, m_reached_fluent_atoms);

    m_index_list.clear();
    valla::encode_as_unsigned_integrals(dense_fluent_numeric_variables, double_leaf_table, std::back_inserter(m_index_list));
    state_numeric_variables = valla::insert_sequence(m_index_list, index_tree_table);

    // Check if non-extended state exists in cache.
    auto it = m_states.find(PackedStateImpl(state_fluent_atoms_slot, state_derived_atoms_slot, state_numeric_variables));
    if (it != m_states.end())
    {
        dense_derived_atoms.unset_all();  ///< Important: now we must clear the buffer before inserting the derived atoms of the successor state.
        m_index_list.clear();
        valla::read_sequence(it->first.get_atoms<DerivedTag>(), index_tree_table, std::back_inserter(m_index_list));
        for (const auto index : m_index_list)
        {
            dense_derived_atoms.set(index);
        }
        auto state = State(it->second, &it->first, std::move(unpacked_state), shared_from_this());
        return { state, successor_state_metric_value };
    }

    /* 3. If necessary, apply axioms to construct extended state. */
    {
        if (!m_axiom_evaluator->get_problem()->get_problem_and_domain_axioms().empty())
        {
            // Evaluate axioms
            dense_derived_atoms.unset_all();  ///< Important: now we must clear the buffer before evaluating for the updated fluent atoms.
            m_axiom_evaluator->generate_and_apply_axioms(*unpacked_state);

            state_derived_atoms_slot = valla::insert_sequence(dense_derived_atoms, index_tree_table);

            update_reached_fluent_atoms(dense_derived_atoms, m_reached_derived_atoms);
        }
    }

    // Cache and return the extended state.
    auto result = m_states.emplace(PackedStateImpl(state_fluent_atoms_slot, state_derived_atoms_slot, state_numeric_variables), m_states.size());
    m_packed_states_by_index.push_back(&result.first->first);
    auto successor_state = State(result.first->second, &result.first->first, std::move(unpacked_state), shared_from_this());

    return { successor_state, successor_state_metric_value };
}

void StateRepositoryImpl::collect_action_add_effect_fluent_atom_indices(const State& state,
                                                                        GroundAction action,
                                                                        iw::AtomIndexList& out_add_fluent_atom_indices)
{
    auto ignored_del_fluent_atom_indices = iw::AtomIndexList {};
    collect_action_change_effect_fluent_atom_indices(state, action, out_add_fluent_atom_indices, ignored_del_fluent_atom_indices);
}

void StateRepositoryImpl::collect_action_change_effect_fluent_atom_indices(const State& state,
                                                                           GroundAction action,
                                                                           iw::AtomIndexList& out_add_fluent_atom_indices,
                                                                           iw::AtomIndexList& out_del_fluent_atom_indices)
{
    m_applied_positive_effect_atoms.unset_all();
    m_applied_negative_effect_atoms.unset_all();
    const auto& unpacked_state = state.get_unpacked_state();
    for (const auto& conditional_effect : action->get_conditional_effects())
    {
        if (is_applicable(conditional_effect, unpacked_state))
        {
            insert_into_bitset(conditional_effect->get_conjunctive_effect()->get_propositional_effects<PositiveTag>(), m_applied_positive_effect_atoms);
            insert_into_bitset(conditional_effect->get_conjunctive_effect()->get_propositional_effects<NegativeTag>(), m_applied_negative_effect_atoms);
        }
    }

    out_add_fluent_atom_indices.clear();
    out_del_fluent_atom_indices.clear();
    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    for (const auto atom_index : m_applied_positive_effect_atoms)
    {
        if (!state_fluent_atoms.get(atom_index))
        {
            out_add_fluent_atom_indices.push_back(atom_index);
        }
    }
    for (const auto atom_index : m_applied_negative_effect_atoms)
    {
        if (state_fluent_atoms.get(atom_index))
        {
            out_del_fluent_atom_indices.push_back(atom_index);
        }
    }
}

StateRepositoryImpl::StagedSuccessorState
StateRepositoryImpl::compute_staged_successor_state(const State& state,
                                                    GroundAction action,
                                                    ContinuousCost state_metric_value,
                                                    StagedSuccessorScratch& scratch) const
{
    const auto& problem = *m_axiom_evaluator->get_problem();

    // Parallel beam workers evaluate successors in dense worker-local storage so they
    // can novelty-test and score states without mutating the shared repository cache.
    auto unpacked_state = scratch.unpacked_state_pool.get_or_allocate(problem);
    auto& dense_fluent_atoms = unpacked_state->get_atoms<FluentTag>();
    dense_fluent_atoms = state.get_unpacked_state().get_atoms<FluentTag>();
    auto& dense_derived_atoms = unpacked_state->get_atoms<DerivedTag>();
    dense_derived_atoms = state.get_unpacked_state().get_atoms<DerivedTag>();
    auto& dense_fluent_numeric_variables = unpacked_state->get_numeric_variables();
    dense_fluent_numeric_variables = state.get_unpacked_state().get_numeric_variables();

    scratch.applied_negative_effect_atoms.unset_all();
    scratch.applied_positive_effect_atoms.unset_all();

    auto successor_state_metric_value = state_metric_value;

    apply_action_effects(action,
                         problem,
                         state,
                         *unpacked_state,
                         dense_fluent_atoms,
                         scratch.applied_negative_effect_atoms,
                         scratch.applied_positive_effect_atoms,
                         dense_fluent_numeric_variables,
                         successor_state_metric_value);

    if (!m_axiom_evaluator->get_problem()->get_problem_and_domain_axioms().empty())
    {
        dense_derived_atoms.unset_all();
        if (m_axiom_evaluator->supports_parallel_staged_successor_evaluation())
        {
            if (!scratch.axiom_worker_context)
            {
                scratch.axiom_worker_context = m_axiom_evaluator->create_parallel_worker_context();
            }
            assert(scratch.axiom_worker_context);
            m_axiom_evaluator->generate_and_apply_axioms_parallel(*unpacked_state, *scratch.axiom_worker_context);
        }
        else
        {
            m_axiom_evaluator->generate_and_apply_axioms(*unpacked_state);
        }
    }

    auto successor_state = StagedSuccessorState();
    successor_state.fluent_atoms = dense_fluent_atoms;
    successor_state.derived_atoms = dense_derived_atoms;
    successor_state.fluent_atom_indices.clear();
    for (const auto index : dense_fluent_atoms)
    {
        successor_state.fluent_atom_indices.push_back(index);
    }
    successor_state.derived_atom_indices.clear();
    for (const auto index : dense_derived_atoms)
    {
        successor_state.derived_atom_indices.push_back(index);
    }
    successor_state.fluent_numeric_variables = dense_fluent_numeric_variables;
    successor_state.metric_value = successor_state_metric_value;
    return successor_state;
}

StateRepositoryImpl::StagedSuccessorHandle
StateRepositoryImpl::get_or_create_staged_successor_handle(const StagedSuccessorState& successor_state, StagedSuccessorInternTimings* timings)
{
    auto& problem = *m_axiom_evaluator->get_problem();
    auto& index_tree_table = problem.get_index_tree_table();
    auto& double_leaf_table = problem.get_double_leaf_table();

    // The main thread materializes worker-computed successors in generation order so
    // state indices, duplicate pruning, and beam admission follow the serial semantics.
    const auto fluent_slot_start = std::chrono::steady_clock::now();
    auto state_fluent_atoms_slot = valla::Slot<Index>();
    if (const auto it = m_fluent_atom_slots.find(successor_state.fluent_atom_indices); it != m_fluent_atom_slots.end())
    {
        state_fluent_atoms_slot = it->second;
    }
    else
    {
        state_fluent_atoms_slot = valla::insert_sequence(successor_state.fluent_atom_indices, index_tree_table);
        m_fluent_atom_slots.emplace(successor_state.fluent_atom_indices, state_fluent_atoms_slot);
    }
    if (timings)
    {
        timings->fluent_slot_time += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - fluent_slot_start);
    }

    m_index_list.clear();
    const auto numeric_slot_start = std::chrono::steady_clock::now();
    valla::encode_as_unsigned_integrals(successor_state.fluent_numeric_variables, double_leaf_table, std::back_inserter(m_index_list));
    auto state_numeric_variables = valla::insert_sequence(m_index_list, index_tree_table);
    if (timings)
    {
        timings->numeric_slot_time += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - numeric_slot_start);
    }

    const auto derived_slot_start = std::chrono::steady_clock::now();
    auto state_derived_atoms_slot = valla::insert_sequence(successor_state.derived_atom_indices, index_tree_table);
    if (timings)
    {
        timings->derived_slot_time += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - derived_slot_start);
    }

    auto packed_state = PackedStateImpl(state_fluent_atoms_slot, state_derived_atoms_slot, state_numeric_variables);
    const auto state_lookup_start = std::chrono::steady_clock::now();
    auto it = m_states.find(packed_state);
    if (timings)
    {
        timings->state_lookup_time += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - state_lookup_start);
    }

    if (it != m_states.end())
    {
        return StagedSuccessorHandle { it->second, &it->first };
    }

    const auto reached_atom_update_start = std::chrono::steady_clock::now();
    update_reached_fluent_atoms(successor_state.fluent_atoms, m_reached_fluent_atoms);
    update_reached_derived_atoms(successor_state.derived_atoms, m_reached_derived_atoms);
    if (timings)
    {
        timings->reached_atom_update_time +=
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - reached_atom_update_start);
    }

    const auto state_emplace_start = std::chrono::steady_clock::now();
    auto result = m_states.emplace(std::move(packed_state), m_states.size());
    m_packed_states_by_index.push_back(&result.first->first);
    if (timings)
    {
        timings->state_lookup_time += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - state_emplace_start);
    }
    return StagedSuccessorHandle { result.first->second, &result.first->first };
}

State StateRepositoryImpl::materialize_staged_successor_state(const StagedSuccessorState& successor_state, const StagedSuccessorHandle& successor_handle)
{
    const auto& problem = *m_axiom_evaluator->get_problem();
    auto unpacked_state = m_unpacked_state_pool.get_or_allocate(problem);
    unpacked_state->get_atoms<FluentTag>() = successor_state.fluent_atoms;
    unpacked_state->get_atoms<DerivedTag>() = successor_state.derived_atoms;
    unpacked_state->get_numeric_variables() = successor_state.fluent_numeric_variables;
    return State(successor_handle.state_index, successor_handle.packed_state, std::move(unpacked_state), shared_from_this());
}

State StateRepositoryImpl::make_temporary_staged_successor_state(const StagedSuccessorState& successor_state,
                                                                 const StagedSuccessorHandle& successor_handle,
                                                                 StagedSuccessorScratch& scratch)
{
    const auto& problem = *m_axiom_evaluator->get_problem();
    auto unpacked_state = scratch.unpacked_state_pool.get_or_allocate(problem);
    unpacked_state->get_atoms<FluentTag>() = successor_state.fluent_atoms;
    unpacked_state->get_atoms<DerivedTag>() = successor_state.derived_atoms;
    unpacked_state->get_numeric_variables() = successor_state.fluent_numeric_variables;
    return State(successor_handle.state_index, successor_handle.packed_state, std::move(unpacked_state), shared_from_this());
}

State StateRepositoryImpl::get_state(const PackedStateImpl& state)
{
    // Unpack the internal state into dense state
    const auto& problem = *m_axiom_evaluator->get_problem();
    auto unpacked_state = m_unpacked_state_pool.get_or_allocate(problem);
    auto& dense_fluent_atoms = unpacked_state->get_atoms<FluentTag>();
    auto& dense_derived_atoms = unpacked_state->get_atoms<DerivedTag>();
    auto& dense_fluent_numeric_variables = unpacked_state->get_numeric_variables();

    dense_fluent_atoms.unset_all();
    m_index_list.clear();
    valla::read_sequence(state.get_atoms<FluentTag>(), problem.get_index_tree_table(), std::back_inserter(m_index_list));
    for (const auto index : m_index_list)
    {
        dense_fluent_atoms.set(index);
    }

    dense_derived_atoms.unset_all();
    m_index_list.clear();
    valla::read_sequence(state.get_atoms<DerivedTag>(), problem.get_index_tree_table(), std::back_inserter(m_index_list));
    for (const auto index : m_index_list)
    {
        dense_derived_atoms.set(index);
    }

    m_index_list.clear();
    valla::read_sequence(state.get_numeric_variables(), problem.get_index_tree_table(), std::back_inserter(m_index_list));
    dense_fluent_numeric_variables.clear();
    valla::decode_from_unsigned_integrals(m_index_list, problem.get_double_leaf_table(), std::back_inserter(dense_fluent_numeric_variables));

    return State(m_states.at(state), &state, std::move(unpacked_state), shared_from_this());
}

Index StateRepositoryImpl::get_state_index(const PackedStateImpl& state) { return m_states.at(state); }

PackedState StateRepositoryImpl::get_packed_state(Index state_index) const { return m_packed_states_by_index.at(state_index); }

const Problem& StateRepositoryImpl::get_problem() const { return m_axiom_evaluator->get_problem(); }

size_t StateRepositoryImpl::get_state_count() const { return m_states.size(); }

const PackedStateImplMap& StateRepositoryImpl::get_states() const { return m_states; }

const FlatBitset& StateRepositoryImpl::get_reached_fluent_ground_atoms_bitset() const { return m_reached_fluent_atoms; }

const FlatBitset& StateRepositoryImpl::get_reached_derived_ground_atoms_bitset() const { return m_reached_derived_atoms; }

const AxiomEvaluator& StateRepositoryImpl::get_axiom_evaluator() const { return m_axiom_evaluator; }
}
