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


#include "mimir/languages/description_logics/constructors.hpp"
#include "mimir/languages/description_logics/equal_to.hpp"
#include "mimir/languages/description_logics/grammar.hpp"
#include "mimir/languages/description_logics/hash.hpp"

#include <loki/loki.hpp>

namespace mimir::dl
{
template<PredicateCategory P>
using ConceptAtomicStateFactory =
    loki::UniqueFactory<ConceptAtomicStateImpl<P>, UniqueDLHasher<const ConceptAtomicStateImpl<P>*>, UniqueDLEqualTo<const ConceptAtomicStateImpl<P>*>>;
template<PredicateCategory P>
using ConceptAtomicGoalFactory =
    loki::UniqueFactory<ConceptAtomicGoalImpl<P>, UniqueDLHasher<const ConceptAtomicGoalImpl<P>*>, UniqueDLEqualTo<const ConceptAtomicGoalImpl<P>*>>;
using ConceptIntersectionFactory =
    loki::UniqueFactory<ConceptIntersectionImpl, UniqueDLHasher<const ConceptIntersectionImpl*>, UniqueDLEqualTo<const ConceptIntersectionImpl*>>;
template<PredicateCategory P>
using RolePredicateStateFactory =
    loki::UniqueFactory<RoleAtomicStateImpl<P>, UniqueDLHasher<const RoleAtomicStateImpl<P>*>, UniqueDLEqualTo<const RoleAtomicStateImpl<P>*>>;
template<PredicateCategory P>
using RolePredicateGoalFactory =
    loki::UniqueFactory<RoleAtomicGoalImpl<P>, UniqueDLHasher<const RoleAtomicGoalImpl<P>*>, UniqueDLEqualTo<const RoleAtomicGoalImpl<P>*>>;
using RoleIntersectionFactory =
    loki::UniqueFactory<RoleIntersectionImpl, UniqueDLHasher<const RoleIntersectionImpl*>, UniqueDLEqualTo<const RoleIntersectionImpl*>>;

using VariadicConstructorFactory = loki::VariadicContainer<ConceptAtomicStateFactory<Static>,
                                                           ConceptAtomicStateFactory<Fluent>,
                                                           ConceptAtomicStateFactory<Derived>,
                                                           ConceptAtomicGoalFactory<Static>,
                                                           ConceptAtomicGoalFactory<Fluent>,
                                                           ConceptAtomicGoalFactory<Derived>,
                                                           ConceptIntersectionFactory,
                                                           RolePredicateStateFactory<Static>,
                                                           RolePredicateStateFactory<Fluent>,
                                                           RolePredicateStateFactory<Derived>,
                                                           RolePredicateGoalFactory<Static>,
                                                           RolePredicateGoalFactory<Fluent>,
                                                           RolePredicateGoalFactory<Derived>,
                                                           RoleIntersectionFactory>;

extern VariadicConstructorFactory create_default_variadic_constructor_factory();

class Generator
{
private:
    /* Memory */
    VariadicConstructorFactory m_constructor_repos;

    /* Grammar specification */
    grammar::Grammar m_grammar;

public:
    Generator(grammar::Grammar grammar);

    /* TODO: refinement operators */
};
}


