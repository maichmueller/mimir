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
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/ground_literal.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/search/action.hpp"
#include "mimir/search/applicable_action_generators.hpp"

namespace mimir
{
StateRepository::StateRepository(std::shared_ptr<IApplicableActionGenerator> applicable_action_generator) :
    m_applicable_action_generator(std::move(applicable_action_generator)),
    m_problem_or_domain_has_axioms(!m_applicable_action_generator->get_problem()->get_axioms().empty()
                                   || !m_applicable_action_generator->get_problem()->get_domain()->get_axioms().empty()),
    m_states(),
    m_state_builder(),
    m_positive_applied_effects(),
    m_negative_applied_effects(),
    m_reached_fluent_atoms(),
    m_reached_derived_atoms()
{
}

State StateRepository::get_or_create_initial_state()
{
    return get_or_create_state(
        ranges::to<GroundAtomList<Fluent>>(std::views::transform(m_applicable_action_generator->get_problem()->get_fluent_initial_literals(),
                                                                 [&](const auto& literal)
                                                                 {
                                                                     if (literal->is_negated())
                                                                     {
                                                                         throw std::runtime_error("negated literals in the initial state are not supported");
                                                                     }
                                                                     return literal->get_atom();
                                                                 })));
}

State StateRepository::get_or_create_state(const GroundAtomList<Fluent>& atoms)
{
    /* Fetch member references for non extended construction. */

    auto& state_index = m_state_builder.get_index();
    auto& fluent_state_atoms = m_state_builder.get_atoms<Fluent>();
    fluent_state_atoms.unset_all();

    /* 1. Set state id */

    int next_state_index = m_states.size();
    state_index = next_state_index;

    /* 2. Construct non-extended state */

    for (const auto& atom : atoms)
    {
        fluent_state_atoms.set(atom->get_index());
    }
    m_reached_fluent_atoms |= fluent_state_atoms;

    /* 3. Retrieve cached extended state */

    // Test whether there exists an extended state for the given non extended state
    auto iter = m_states.find(m_state_builder);
    if (iter != m_states.end())
    {
        return *iter;
    }

    /* Return early, if no axioms must be evaluated. */
    if (!m_problem_or_domain_has_axioms)
    {
        auto [iter2, inserted] = m_states.insert(m_state_builder);
        return *iter2;
    }

    /* Fetch member references for extended construction. */

    auto& derived_state_atoms = m_state_builder.get_atoms<Derived>();
    derived_state_atoms.unset_all();

    /* 4. Construct extended state by evaluating Axioms */

    m_applicable_action_generator->generate_and_apply_axioms(m_state_builder);
    m_reached_derived_atoms |= derived_state_atoms;

    /* 5. Cache extended state */

    auto [iter2, inserted] = m_states.insert(m_state_builder);

    /* 6. Return newly generated extended state */

    return *iter2;
}

std::pair<State, ContinuousCost> StateRepository::get_or_create_successor_state(State state, GroundAction action)
{
    /* Accumulate state-dependent action cost. */
    auto action_cost = ContinuousCost(0);

    /* 1. Set the state index. */

    {
        auto& state_index = m_state_builder.get_index();
        const auto next_state_index = Index(m_states.size());
        state_index = next_state_index;
    }

    /* 2. Apply action effects to construct non-extended state. */

    {
        // Apply STRIPS effect
        const auto& strips_action_effect = action->get_strips_effect();
        m_negative_applied_effects = strips_action_effect.get_negative_effects();
        m_positive_applied_effects = strips_action_effect.get_positive_effects();
        action_cost += strips_action_effect.get_cost();

        // Apply conditional effects
        for (const auto& conditional_effect : action->get_conditional_effects())
        {
            if (conditional_effect.is_applicable(m_applicable_action_generator->get_problem(), state))
            {
                for (const auto& effect_literal : conditional_effect.get_fluent_effect_literals())
                {
                    if (effect_literal.is_negated)
                    {
                        m_negative_applied_effects.set(effect_literal.atom_index);
                    }
                    else
                    {
                        m_positive_applied_effects.set(effect_literal.atom_index);
                    }
                }
                action_cost += conditional_effect.get_cost();
            }
        }

        // Modify fluent state atoms
        auto& fluent_state_atoms = m_state_builder.get_atoms<Fluent>();
        fluent_state_atoms = state->get_atoms<Fluent>();
        fluent_state_atoms -= m_negative_applied_effects;
        fluent_state_atoms |= m_positive_applied_effects;

        // Update reached fluent atoms
        m_reached_fluent_atoms |= fluent_state_atoms;

        // Check if non-extended state exists in cache
        const auto iter = m_states.find(m_state_builder);
        if (iter != m_states.end())
        {
            return std::make_pair(*iter, action_cost);
        }
    }

    /* 3. Apply axioms to construct extended state. */

    {
        // Return early if no axioms must be evaluated
        if (!m_problem_or_domain_has_axioms)
        {
            const auto [iter2, inserted] = m_states.insert(m_state_builder);
            return std::make_pair(*iter2, action_cost);
        }

        // Evaluate axioms
        auto& derived_state_atoms = m_state_builder.get_atoms<Derived>();
        derived_state_atoms.unset_all();
        m_applicable_action_generator->generate_and_apply_axioms(m_state_builder);

        // Update reached derived atoms
        m_reached_derived_atoms |= derived_state_atoms;
    }

    // Cache and return the extended state.
    return std::make_pair(*m_states.insert(m_state_builder).first, action_cost);
}

size_t StateRepository::get_state_count() const { return m_states.size(); }

const FlatBitset& StateRepository::get_reached_fluent_ground_atoms_bitset() const { return m_reached_fluent_atoms; }

const FlatBitset& StateRepository::get_reached_derived_ground_atoms_bitset() const { return m_reached_derived_atoms; }

const std::shared_ptr<IApplicableActionGenerator>& StateRepository::get_applicable_action_generator() const { return m_applicable_action_generator; }
}
