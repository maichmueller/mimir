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

#ifndef MIMIR_FORMALISM_TRANSLATORS_DISJUNCTIVE_NORMAL_FORM_HPP_
#define MIMIR_FORMALISM_TRANSLATORS_DISJUNCTIVE_NORMAL_FORM_HPP_

#include "mimir/formalism/translators/base.hpp"
#include "mimir/formalism/translators/to_negation_normal_form.hpp"

namespace mimir
{
/**
 * Translate formulas to disjunctive normal form (DNF) in bottom-up manner using the following rules.
 *
 * First, applies NNF translator follow by:
 *
 * 1. A and (B or C)  =>  A and B or A and C
 * 2. exists(vars, A or B)  =>  exists(vars, A) or exists(vars, B)
 * 3. forall(vars, A or B)  =>  forall(vars, A) or forall(vars, B)
 */
class ToDNFTranslator : public BaseTranslator<ToDNFTranslator>
{
private:
    /* Implement BaseTranslator interface. */
    friend class BaseTranslator<ToDNFTranslator>;

    // Provide default implementations
    using BaseTranslator::prepare_impl;
    using BaseTranslator::translate_impl;

    ToNNFTranslator& m_to_nnf_translator;

    /**
     * Translate
     */

    /**
     * Apply conjunctive disjunctive distributivity.
     *
     * 1. A and (B or C)  =>  A and B or A and C
     */
    Condition translate_impl(const ConditionAndImpl& condition);
    /**
     * 2. exists(vars, A or B)  =>  exists(vars, A) or exists(vars, B)
     */
    Condition translate_impl(const ConditionExistsImpl& condition);
    /**
     * 3. forall(vars, A or B)  =>  forall(vars, A) or forall(vars, B)
     */
    Condition translate_impl(const ConditionForallImpl& condition);

    Problem run_impl(const ProblemImpl& problem);

public:
    ToDNFTranslator(PDDLFactories& pddl_factories, ToNNFTranslator& to_nnf_translator);
};

}

#endif