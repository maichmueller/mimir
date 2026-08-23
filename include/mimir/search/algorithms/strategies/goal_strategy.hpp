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

#ifndef MIMIR_SEARCH_ALGORITHMS_STRATEGIES_GOAL_STRATEGY_HPP_
#define MIMIR_SEARCH_ALGORITHMS_STRATEGIES_GOAL_STRATEGY_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/search/declarations.hpp"

#include <ranges>

namespace mimir::search
{

/// @brief `IGoalStrategy` encapsulates logic to test whether a state is a goal.
class IGoalStrategy
{
public:
    virtual ~IGoalStrategy() = default;

    virtual bool test_static_goal() = 0;
    virtual bool test_dynamic_goal(const State& state) = 0;
};


/// @brief `ProblemGoalStrategyImpl` identifies a state as a goal if and only if it satisfies the goal in the given problem.
class ProblemGoalStrategyImpl : public IGoalStrategy
{
private:
    formalism::Problem m_problem;
    formalism::GroundConjunctiveCondition m_condition;
    const bool m_static_goal_holds;

    bool _compute_static_goal_holds() const;
public:
    explicit ProblemGoalStrategyImpl(formalism::Problem problem, std::optional<formalism::GroundConjunctiveCondition> condition = std::nullopt);

    bool test_static_goal() override;
    bool test_dynamic_goal(const State& state) override;

    static ProblemGoalStrategy create(formalism::Problem problem,std::optional<formalism::GroundConjunctiveCondition> condition = std::nullopt);
};

/// @brief `ProblemMultiGoalStrategyImpl` identifies a state as a goal if any of the given goal conditions is satisfied.
class ProblemMultiGoalStrategyImpl : public IGoalStrategy
{
private:
    formalism::Problem m_problem;
    std::vector<formalism::GroundConjunctiveCondition> m_conditions;
    std::vector<bool> m_static_goal_holds;
    bool m_any_static_goal_holds;

    bool _compute_static_goal_holds(formalism::GroundConjunctiveCondition condition) const;

public:
    explicit ProblemMultiGoalStrategyImpl(formalism::Problem problem, std::vector<formalism::GroundConjunctiveCondition> conditions);

    bool test_static_goal() override;
    bool test_dynamic_goal(const State& state) override;

    static ProblemMultiGoalStrategy create(formalism::Problem problem, std::vector<formalism::GroundConjunctiveCondition> conditions);

    template<std::ranges::input_range Range>
    static ProblemMultiGoalStrategy create(formalism::Problem problem, Range&& conditions)
    {
        auto condition_list = std::vector<formalism::GroundConjunctiveCondition> {};
        for (const auto condition : conditions)
        {
            condition_list.push_back(condition);
        }
        return create(problem, std::move(condition_list));
    }
};
}

#endif
