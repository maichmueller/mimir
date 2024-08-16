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

#include "mimir/languages/description_logics/constructors.hpp"

#include "mimir/common/hash.hpp"

namespace mimir::dl
{

/**
 * ConceptPredicateState
 */

template<PredicateCategory P>
ConceptPredicateStateImpl<P>::ConceptPredicateStateImpl(size_t index, Predicate<P> predicate) : m_index(index), m_predicate(predicate)
{
}

template<PredicateCategory P>
void ConceptPredicateStateImpl<P>::evaluate_impl(EvaluationContext& context) const
{
    // Fetch data
    auto& bitset = context.get_denotation_builder<Concept>().get_bitset();
    bitset.unset_all();

    // Compute result
    for (const auto& atom : context.get_state_atoms<P>())
    {
        if (atom->get_predicate() == m_predicate)
        {
            bitset.set(atom->get_objects().at(0)->get_index());
        }
    }
}

template<>
void ConceptPredicateStateImpl<Static>::evaluate_impl(EvaluationContext& context) const
{
    // Fetch data
    auto& bitset = context.get_denotation_builder<Concept>().get_bitset();
    bitset.unset_all();

    // Compute result
    for (const auto& atom : context.get_state_atoms<Static>())
    {
        if (atom->get_predicate() == m_predicate)
        {
            bitset.set(atom->get_index());
        }
    }
}

template<PredicateCategory P>
bool ConceptPredicateStateImpl<P>::accept_impl(const grammar::Visitor<Concept>& visitor) const
{
    return visitor.visit(this);
}

template<PredicateCategory P>
size_t ConceptPredicateStateImpl<P>::get_index() const
{
    return m_index;
}

template<PredicateCategory P>
Predicate<P> ConceptPredicateStateImpl<P>::get_predicate() const
{
    return m_predicate;
}

template class ConceptPredicateStateImpl<Static>;
template class ConceptPredicateStateImpl<Fluent>;
template class ConceptPredicateStateImpl<Derived>;

/**
 * ConceptPredicateGoal
 */

template<PredicateCategory P>
ConceptPredicateGoalImpl<P>::ConceptPredicateGoalImpl(size_t index, Predicate<P> predicate) : m_index(index), m_predicate(predicate)
{
}

template<PredicateCategory P>
void ConceptPredicateGoalImpl<P>::evaluate_impl(EvaluationContext& context) const
{
    // Fetch data
    auto& bitset = context.get_denotation_builder<Concept>().get_bitset();
    bitset.unset_all();

    // Compute result
    for (const auto& atom : context.get_goal_atoms<P>())
    {
        if (atom->get_predicate() == m_predicate)
        {
            bitset.set(atom->get_objects().at(0)->get_index());
        }
    }
}

template<PredicateCategory P>
bool ConceptPredicateGoalImpl<P>::accept_impl(const grammar::Visitor<Concept>& visitor) const
{
    return visitor.visit(this);
}

template<PredicateCategory P>
size_t ConceptPredicateGoalImpl<P>::get_index() const
{
    return m_index;
}

template<PredicateCategory P>
Predicate<P> ConceptPredicateGoalImpl<P>::get_predicate() const
{
    return m_predicate;
}

template class ConceptPredicateGoalImpl<Static>;
template class ConceptPredicateGoalImpl<Fluent>;
template class ConceptPredicateGoalImpl<Derived>;

/**
 * ConceptAnd
 */

ConceptAndImpl::ConceptAndImpl(size_t index, Constructor<Concept> concept_left, Constructor<Concept> concept_right) :
    m_index(index),
    m_concept_left(concept_left),
    m_concept_right(concept_right)
{
}

void ConceptAndImpl::evaluate_impl(EvaluationContext& context) const
{
    // Evaluate children
    const auto eval_left = m_concept_left->evaluate(context);
    const auto eval_right = m_concept_left->evaluate(context);

    // Fetch data
    auto& bitset = context.get_denotation_builder<Concept>().get_bitset();
    bitset.unset_all();

    // Compute result
    bitset |= eval_left.get_bitset();
    bitset &= eval_right.get_bitset();
}

bool ConceptAndImpl::accept_impl(const grammar::Visitor<Concept>& visitor) const { return visitor.visit(this); }

size_t ConceptAndImpl::get_index() const { return m_index; }

Constructor<Concept> ConceptAndImpl::get_concept_left() const { return m_concept_left; }

Constructor<Concept> ConceptAndImpl::get_concept_right() const { return m_concept_right; }

/**
 * RolePredicateState
 */

template<PredicateCategory P>
RolePredicateStateImpl<P>::RolePredicateStateImpl(size_t index, Predicate<P> predicate) : m_index(index), m_predicate(predicate)
{
}

template<PredicateCategory P>
void RolePredicateStateImpl<P>::evaluate_impl(EvaluationContext& context) const
{
    // Fetch data
    auto& bitsets = context.get_denotation_builder<Role>().get_bitsets();
    for (auto& bitset : bitsets)
    {
        bitset.unset_all();
    }

    // Compute result
    for (const auto& atom : context.get_state_atoms<P>())
    {
        if (atom->get_predicate() == m_predicate)
        {
            const auto object_left_id = atom->get_objects().at(0)->get_index();
            const auto object_right_id = atom->get_objects().at(1)->get_index();
            bitsets.at(object_left_id).set(object_right_id);
        }
    }
}

template<>
void RolePredicateStateImpl<Static>::evaluate_impl(EvaluationContext& context) const
{
    // Fetch data
    auto& bitsets = context.get_denotation_builder<Role>().get_bitsets();
    for (auto& bitset : bitsets)
    {
        bitset.unset_all();
    }

    // Compute result
    for (const auto& atom : context.get_state_atoms<Static>())
    {
        if (atom->get_predicate() == m_predicate)
        {
            const auto object_left_id = atom->get_objects().at(0)->get_index();
            const auto object_right_id = atom->get_objects().at(1)->get_index();
            bitsets.at(object_left_id).set(object_right_id);
        }
    }
}

template<PredicateCategory P>
bool RolePredicateStateImpl<P>::accept_impl(const grammar::Visitor<Role>& visitor) const
{
    return visitor.visit(this);
}

template<PredicateCategory P>
size_t RolePredicateStateImpl<P>::get_index() const
{
    return m_index;
}

template<PredicateCategory P>
Predicate<P> RolePredicateStateImpl<P>::get_predicate() const
{
    return m_predicate;
}

template class RolePredicateStateImpl<Static>;
template class RolePredicateStateImpl<Fluent>;
template class RolePredicateStateImpl<Derived>;

/**
 * RolePredicateGoal
 */

template<PredicateCategory P>
RolePredicateGoalImpl<P>::RolePredicateGoalImpl(size_t index, Predicate<P> predicate) : m_index(index), m_predicate(predicate)
{
}

template<PredicateCategory P>
void RolePredicateGoalImpl<P>::evaluate_impl(EvaluationContext& context) const
{
    // Fetch data
    auto& bitsets = context.get_denotation_builder<Role>().get_bitsets();
    for (auto& bitset : bitsets)
    {
        bitset.unset_all();
    }

    // Compute result
    for (const auto& atom : context.get_goal_atoms<P>())
    {
        if (atom->get_predicate() == m_predicate)
        {
            const auto object_left_id = atom->get_objects().at(0)->get_index();
            const auto object_right_id = atom->get_objects().at(1)->get_index();
            bitsets.at(object_left_id).set(object_right_id);
        }
    }
}

template<PredicateCategory P>
bool RolePredicateGoalImpl<P>::accept_impl(const grammar::Visitor<Role>& visitor) const
{
    return visitor.visit(this);
}

template<PredicateCategory P>
size_t RolePredicateGoalImpl<P>::get_index() const
{
    return m_index;
}

template<PredicateCategory P>
Predicate<P> RolePredicateGoalImpl<P>::get_predicate() const
{
    return m_predicate;
}

template class RolePredicateGoalImpl<Static>;
template class RolePredicateGoalImpl<Fluent>;
template class RolePredicateGoalImpl<Derived>;

/**
 * RoleAnd
 */

RoleAndImpl::RoleAndImpl(size_t index, Constructor<Role> role_left, Constructor<Role> role_right) :
    m_index(index),
    m_role_left(role_left),
    m_role_right(role_right)
{
}

void RoleAndImpl::evaluate_impl(EvaluationContext& context) const
{
    // Evaluate children
    const auto eval_left = m_role_left->evaluate(context);
    const auto eval_right = m_role_left->evaluate(context);

    // Fetch data
    auto& bitsets = context.get_denotation_builder<Role>().get_bitsets();
    for (auto& bitset : bitsets)
    {
        bitset.unset_all();
    }

    // Compute result
    for (size_t i = 0; i < eval_left.get_bitsets().size(); ++i)
    {
        auto& bitset = bitsets.at(i);
        bitset |= eval_left.get_bitsets().at(i);
        bitset &= eval_right.get_bitsets().at(i);
    }
}

bool RoleAndImpl::accept_impl(const grammar::Visitor<Role>& visitor) const { return visitor.visit(this); }

size_t RoleAndImpl::get_index() const { return m_index; }

Constructor<Role> RoleAndImpl::get_role_left() const { return m_role_left; }

Constructor<Role> RoleAndImpl::get_role_right() const { return m_role_right; }
}
