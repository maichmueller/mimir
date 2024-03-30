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

#include "mimir/formalism/translators/negation_normal_form.hpp"

namespace mimir
{

NNFTranslator::ConditionNotVisitor::ConditionNotVisitor(NNFTranslator& translator) : m_translator(translator) {}

Condition NNFTranslator::ConditionNotVisitor::operator()(const ConditionLiteralImpl& condition) { return m_translator.translate(condition); }

Condition NNFTranslator::ConditionNotVisitor::operator()(const ConditionNotImpl& condition) { return m_translator.translate(*condition.get_condition()); }

Condition NNFTranslator::ConditionNotVisitor::operator()(const ConditionAndImpl& condition)
{
    auto translated_nested_conditions = ConditionList {};
    translated_nested_conditions.reserve(condition.get_conditions().size());
    for (const auto& nested_condition : condition.get_conditions())
    {
        translated_nested_conditions.push_back(
            m_translator.m_pddl_factories.conditions.template get_or_create<ConditionNotImpl>(m_translator.translate(*nested_condition)));
    }
    return m_translator.m_pddl_factories.conditions.template get_or_create<ConditionOrImpl>(translated_nested_conditions);
}

Condition NNFTranslator::ConditionNotVisitor::operator()(const ConditionOrImpl& condition)
{
    auto translated_nested_conditions = ConditionList {};
    translated_nested_conditions.reserve(condition.get_conditions().size());
    for (const auto& nested_condition : condition.get_conditions())
    {
        translated_nested_conditions.push_back(
            m_translator.m_pddl_factories.conditions.template get_or_create<ConditionNotImpl>(m_translator.translate(*nested_condition)));
    }
    return m_translator.m_pddl_factories.conditions.template get_or_create<ConditionAndImpl>(translated_nested_conditions);
}

Condition NNFTranslator::ConditionNotVisitor::operator()(const ConditionExistsImpl& condition)
{
    return m_translator.m_pddl_factories.conditions.template get_or_create<ConditionForallImpl>(
        condition.get_parameters(),
        m_translator.m_pddl_factories.conditions.template get_or_create<ConditionNotImpl>(m_translator.translate(*condition.get_condition())));
}

Condition NNFTranslator::ConditionNotVisitor::operator()(const ConditionForallImpl& condition)
{
    return m_translator.m_pddl_factories.conditions.template get_or_create<ConditionExistsImpl>(
        condition.get_parameters(),
        m_translator.m_pddl_factories.conditions.template get_or_create<ConditionNotImpl>(m_translator.translate(*condition.get_condition())));
}

Condition NNFTranslator::translate_impl(const ConditionImplyImpl& condition)
{
    return this->m_pddl_factories.conditions.template get_or_create<ConditionOrImpl>(
        ConditionList { this->m_pddl_factories.conditions.template get_or_create<ConditionNotImpl>(this->translate(*condition.get_condition_left())),
                        this->translate(*condition.get_condition_right()) });
}

Condition NNFTranslator::translate_impl(const ConditionNotImpl& condition)
{
    auto current = Condition { nullptr };

    while (true)
    {
        auto translated = std::visit(NNFTranslator::ConditionNotVisitor(*this), *this->translate(*condition.get_condition()));

        if (current == translated)
        {
            break;
        }
        current = translated;
    }
    return current;
}

Problem NNFTranslator::run_impl(const ProblemImpl& problem) { return this->translate(problem); }

NNFTranslator::NNFTranslator(PDDLFactories& pddl_factories) : BaseTranslator(pddl_factories) {}

}