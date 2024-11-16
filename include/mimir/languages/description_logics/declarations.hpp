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

#pragma once


#include "mimir/formalism/predicate_category.hpp"
#include "mimir/languages/description_logics/constructor_category.hpp"

#include <variant>
#include <vector>

namespace mimir::dl
{
template<typename T>
class ConstructorRepository;

class EvaluationContext;

/**
 * Denotations
 */

template<IsConceptOrRole D>
struct DenotationImpl;
template<IsConceptOrRole D>
using Denotation = const DenotationImpl<D>*;

/**
 * Constructors
 */

/* Constructor */
template<IsConceptOrRole D>
class ConstructorImpl;
template<IsConceptOrRole D>
using Constructor = const ConstructorImpl<D>*;
template<IsConceptOrRole D>
using ConstructorList = std::vector<Constructor<D>>;

/* Concrete concepts */
class ConceptTopImpl;
using ConceptTop = const ConceptTopImpl*;
class ConceptBotImpl;
using ConceptBot = const ConceptBotImpl*;
template<PredicateCategory P>
class ConceptAtomicStateImpl;
template<PredicateCategory P>
using ConceptAtomicState = const ConceptAtomicStateImpl<P>*;
template<PredicateCategory P>
class ConceptAtomicGoalImpl;
template<PredicateCategory P>
using ConceptAtomicGoal = const ConceptAtomicGoalImpl<P>*;
class ConceptIntersectionImpl;
using ConceptIntersection = const ConceptIntersectionImpl*;
class ConceptUnionImpl;
using ConceptUnion = const ConceptUnionImpl*;
class ConceptNegationImpl;
using ConceptNegation = const ConceptNegationImpl*;
class ConceptValueRestrictionImpl;
using ConceptValueRestriction = const ConceptValueRestrictionImpl*;
class ConceptExistentialQuantificationImpl;
using ConceptExistentialQuantification = const ConceptExistentialQuantificationImpl*;
class ConceptRoleValueMapContainmentImpl;
using ConceptRoleValueMapContainment = const ConceptRoleValueMapContainmentImpl*;
class ConceptRoleValueMapEqualityImpl;
using ConceptRoleValueMapEquality = const ConceptRoleValueMapEqualityImpl*;
class ConceptNominalImpl;
using ConceptNominal = const ConceptNominalImpl*;

/* Concrete roles */
class RoleUniversalImpl;
using RoleUniversal = const RoleUniversalImpl*;
template<PredicateCategory P>
class RoleAtomicStateImpl;
template<PredicateCategory P>
using RoleAtomicState = const RoleAtomicStateImpl<P>*;
template<PredicateCategory P>
class RoleAtomicGoalImpl;
template<PredicateCategory P>
using RoleAtomicGoal = const RoleAtomicGoalImpl<P>*;
class RoleIntersectionImpl;
using RoleIntersection = const RoleIntersectionImpl*;
class RoleUnionImpl;
using RoleUnion = const RoleUnionImpl*;
class RoleComplementImpl;
using RoleComplement = const RoleComplementImpl*;
class RoleInverseImpl;
using RoleInverse = const RoleInverseImpl*;
class RoleCompositionImpl;
using RoleComposition = const RoleCompositionImpl*;
class RoleTransitiveClosureImpl;
using RoleTransitiveClosure = const RoleTransitiveClosureImpl*;
class RoleReflexiveTransitiveClosureImpl;
using RoleReflexiveTransitiveClosure = const RoleReflexiveTransitiveClosureImpl*;
class RoleRestrictionImpl;
using RoleRestriction = const RoleRestrictionImpl*;
class RoleIdentityImpl;
using RoleIdentity = const RoleIdentityImpl*;

namespace grammar
{
/**
 * Constructors
 */

/* Constructor */
template<dl::IsConceptOrRole D>
class ConstructorImpl;
template<dl::IsConceptOrRole D>
using Constructor = const ConstructorImpl<D>*;

/* DerivationRule */
template<dl::IsConceptOrRole D>
class DerivationRuleImpl;
template<dl::IsConceptOrRole D>
using DerivationRule = const DerivationRuleImpl<D>*;
template<IsConceptOrRole D>
using DerivationRuleList = std::vector<DerivationRule<D>>;

/* NonTerminal */
template<dl::IsConceptOrRole D>
class NonTerminalImpl;
template<dl::IsConceptOrRole D>
using NonTerminal = const NonTerminalImpl<D>*;

/* ConstructorOrNonTerminalChoice */
template<IsConceptOrRole D>
using ConstructorOrNonTerminalChoice = std::variant<Constructor<D>, NonTerminal<D>>;

/* Choice */
template<dl::IsConceptOrRole D>
class ChoiceImpl;
template<dl::IsConceptOrRole D>
using Choice = const ChoiceImpl<D>*;
template<dl::IsConceptOrRole D>
using ChoiceList = std::vector<Choice<D>>;

/* Concrete concepts */
class ConceptTopImpl;
using ConceptTop = const ConceptTopImpl*;
class ConceptBotImpl;
using ConceptBot = const ConceptBotImpl*;
template<PredicateCategory P>
class ConceptAtomicStateImpl;
template<PredicateCategory P>
using ConceptAtomicState = const ConceptAtomicStateImpl<P>*;
template<PredicateCategory P>
class ConceptAtomicGoalImpl;
template<PredicateCategory P>
using ConceptAtomicGoal = const ConceptAtomicGoalImpl<P>*;
class ConceptIntersectionImpl;
using ConceptIntersection = const ConceptIntersectionImpl*;
class ConceptUnionImpl;
using ConceptUnion = const ConceptUnionImpl*;
class ConceptNegationImpl;
using ConceptNegation = const ConceptNegationImpl*;
class ConceptValueRestrictionImpl;
using ConceptValueRestriction = const ConceptValueRestrictionImpl*;
class ConceptExistentialQuantificationImpl;
using ConceptExistentialQuantification = const ConceptExistentialQuantificationImpl*;
class ConceptRoleValueMapContainmentImpl;
using ConceptRoleValueMapContainment = const ConceptRoleValueMapContainmentImpl*;
class ConceptRoleValueMapEqualityImpl;
using ConceptRoleValueMapEquality = const ConceptRoleValueMapEqualityImpl*;
class ConceptNominalImpl;
using ConceptNominal = const ConceptNominalImpl*;

/* Concrete roles */
class RoleUniversalImpl;
using RoleUniversal = const RoleUniversalImpl*;
template<PredicateCategory P>
class RoleAtomicStateImpl;
template<PredicateCategory P>
using RoleAtomicState = const RoleAtomicStateImpl<P>*;
template<PredicateCategory P>
class RoleAtomicGoalImpl;
template<PredicateCategory P>
using RoleAtomicGoal = const RoleAtomicGoalImpl<P>*;
class RoleIntersectionImpl;
using RoleIntersection = const RoleIntersectionImpl*;
class RoleUnionImpl;
using RoleUnion = const RoleUnionImpl*;
class RoleComplementImpl;
using RoleComplement = const RoleComplementImpl*;
class RoleInverseImpl;
using RoleInverse = const RoleInverseImpl*;
class RoleCompositionImpl;
using RoleComposition = const RoleCompositionImpl*;
class RoleTransitiveClosureImpl;
using RoleTransitiveClosure = const RoleTransitiveClosureImpl*;
class RoleReflexiveTransitiveClosureImpl;
using RoleReflexiveTransitiveClosure = const RoleReflexiveTransitiveClosureImpl*;
class RoleRestrictionImpl;
using RoleRestriction = const RoleRestrictionImpl*;
class RoleIdentityImpl;
using RoleIdentity = const RoleIdentityImpl*;

/**
 * Visitors
 */

template<dl::IsConceptOrRole D>
class Visitor;

}
}

