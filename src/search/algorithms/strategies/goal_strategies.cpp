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

#include "mimir/formalism/problem.hpp"
#include "mimir/search/algorithms/strategies/goal_strategy.hpp"
#include "mimir/search/applicability.hpp"
#include "mimir/search/state.hpp"

#include <algorithm>

using namespace mimir::formalism;

namespace mimir::search
{

static bool compute_static_goal_holds(Problem problem, GroundConjunctiveCondition condition)
{
    const auto& initial_bitset = problem->get_positive_static_initial_atoms_bitset();

    for (const Index atom_index : condition->get_compressed_precondition<PositiveTag, StaticTag>()->compressed_range())
    {
        if (!initial_bitset.get(atom_index))
        {
            return false;
        }
    }

    for (const Index atom_index : condition->get_compressed_precondition<NegativeTag, StaticTag>()->compressed_range())
    {
        if (initial_bitset.get(atom_index))
        {
            return false;
        }
    }

    return true;
}

bool ProblemGoalStrategyImpl::_compute_static_goal_holds() const
{
    return compute_static_goal_holds(m_problem, m_condition);
}

ProblemGoalStrategyImpl::ProblemGoalStrategyImpl(Problem problem, std::optional<GroundConjunctiveCondition> condition) :
    m_problem(problem),
    m_condition(condition ? *condition : m_problem->get_goal_condition()),
    m_static_goal_holds(condition ? _compute_static_goal_holds() : m_problem->static_goal_holds())
{
}

bool ProblemGoalStrategyImpl::test_static_goal() { return m_static_goal_holds; }

bool ProblemGoalStrategyImpl::test_dynamic_goal(const State& state) { return is_dynamically_applicable(m_condition, state); }

ProblemGoalStrategy ProblemGoalStrategyImpl::create(Problem problem, std::optional<GroundConjunctiveCondition> condition)
{ return std::make_shared<ProblemGoalStrategyImpl>(problem, condition); }

ProblemMultiGoalStrategyImpl::ProblemMultiGoalStrategyImpl(Problem problem, std::vector<GroundConjunctiveCondition> conditions) :
    m_problem(problem),
    m_conditions(std::move(conditions)),
    m_static_goal_holds(),
    m_any_static_goal_holds(false)
{
    m_static_goal_holds.reserve(m_conditions.size());

    for (const auto condition : m_conditions)
    {
        const auto static_goal_holds = compute_static_goal_holds(m_problem, condition);
        m_static_goal_holds.push_back(static_goal_holds);
        m_any_static_goal_holds = m_any_static_goal_holds || static_goal_holds;
    }
}

bool ProblemMultiGoalStrategyImpl::_compute_static_goal_holds(GroundConjunctiveCondition condition) const
{
    return compute_static_goal_holds(m_problem, condition);
}

bool ProblemMultiGoalStrategyImpl::test_static_goal() { return m_any_static_goal_holds; }

bool ProblemMultiGoalStrategyImpl::test_dynamic_goal(const State& state)
{
    for (size_t i = 0; i < m_conditions.size(); ++i)
    {
        if (m_static_goal_holds[i] && is_dynamically_applicable(m_conditions[i], state))
        {
            return true;
        }
    }

    return false;
}

ProblemMultiGoalStrategy ProblemMultiGoalStrategyImpl::create(Problem problem, std::vector<GroundConjunctiveCondition> conditions)
{
    return std::make_shared<ProblemMultiGoalStrategyImpl>(problem, std::move(conditions));
}
}
