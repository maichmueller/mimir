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

#include "mimir/search/algorithms/strategies/layer_ordering_strategy.hpp"

#include "mimir/common/types_cista.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/search/state.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace mimir::formalism;

namespace mimir::search
{

bool ILayerOrderingStrategy::supports_eager_scoring() const { return false; }

ContinuousCost ILayerOrderingStrategy::score_state(const State& state, DiscreteCost g_value) const
{
    [[maybe_unused]] const auto& ignored_state = state;
    [[maybe_unused]] const auto ignored_g_value = g_value;
    throw std::logic_error("ILayerOrderingStrategy does not support eager scoring.");
}

bool ILayerOrderingStrategy::supports_staged_scoring() const { return false; }

ContinuousCost ILayerOrderingStrategy::score_staged_state(const FlatBitset& fluent_atoms,
                                                          const FlatBitset& derived_atoms,
                                                          const FlatDoubleList& numeric_variables,
                                                          DiscreteCost g_value) const
{
    [[maybe_unused]] const auto& ignored_fluent_atoms = fluent_atoms;
    [[maybe_unused]] const auto& ignored_derived_atoms = derived_atoms;
    [[maybe_unused]] const auto& ignored_numeric_variables = numeric_variables;
    [[maybe_unused]] const auto ignored_g_value = g_value;
    throw std::logic_error("ILayerOrderingStrategy does not support staged eager scoring.");
}

bool ILayerOrderingStrategy::prefer_higher_scores() const { return true; }

void InOrderLayerOrderingStrategyImpl::order_layer(StateList& states, DiscreteCost g_value)
{
    [[maybe_unused]] auto& ignored_states = states;
    [[maybe_unused]] auto ignored_g_value = g_value;
}

InOrderLayerOrderingStrategy InOrderLayerOrderingStrategyImpl::create() { return std::make_shared<InOrderLayerOrderingStrategyImpl>(); }

void ReverseOrderLayerOrderingStrategyImpl::order_layer(StateList& states, DiscreteCost g_value)
{
    [[maybe_unused]] auto ignored_g_value = g_value;
    std::reverse(states.begin(), states.end());
}

ReverseOrderLayerOrderingStrategy ReverseOrderLayerOrderingStrategyImpl::create() { return std::make_shared<ReverseOrderLayerOrderingStrategyImpl>(); }

RandomizedLayerOrderingStrategyImpl::RandomizedLayerOrderingStrategyImpl(std::optional<uint64_t> seed) :
    m_rng(seed.value_or(static_cast<uint64_t>(std::random_device {}())))
{
}

void RandomizedLayerOrderingStrategyImpl::order_layer(StateList& states, DiscreteCost g_value)
{
    [[maybe_unused]] auto ignored_g_value = g_value;
    std::shuffle(states.begin(), states.end(), m_rng);
}

RandomizedLayerOrderingStrategy RandomizedLayerOrderingStrategyImpl::create(std::optional<uint64_t> seed)
{ return std::make_shared<RandomizedLayerOrderingStrategyImpl>(seed); }

GoalCountLayerOrderingStrategyImpl::GoalCountLayerOrderingStrategyImpl(formalism::Problem problem, bool prefer_more_satisfied_goals) :
    m_problem(std::move(problem)),
    m_prefer_more_satisfied_goals(prefer_more_satisfied_goals)
{
}

bool GoalCountLayerOrderingStrategyImpl::supports_eager_scoring() const { return true; }

ContinuousCost GoalCountLayerOrderingStrategyImpl::score_state(const State& state, DiscreteCost g_value) const
{
    [[maybe_unused]] const auto ignored_g_value = g_value;
    return static_cast<ContinuousCost>(count_satisfied_goal_literals(state));
}

bool GoalCountLayerOrderingStrategyImpl::supports_staged_scoring() const { return true; }

ContinuousCost GoalCountLayerOrderingStrategyImpl::score_staged_state(const FlatBitset& fluent_atoms,
                                                                      const FlatBitset& derived_atoms,
                                                                      const FlatDoubleList& numeric_variables,
                                                                      DiscreteCost g_value) const
{
    [[maybe_unused]] const auto& ignored_numeric_variables = numeric_variables;
    [[maybe_unused]] const auto ignored_g_value = g_value;

    const auto& positive_fluent_goals = m_problem->get_goal_atoms_bitset<PositiveTag, FluentTag>();
    const auto& negative_fluent_goals = m_problem->get_goal_atoms_bitset<NegativeTag, FluentTag>();
    const auto& positive_derived_goals = m_problem->get_goal_atoms_bitset<PositiveTag, DerivedTag>();
    const auto& negative_derived_goals = m_problem->get_goal_atoms_bitset<NegativeTag, DerivedTag>();

    size_t score = 0;
    score += count_set_intersection(fluent_atoms, positive_fluent_goals);
    score += negative_fluent_goals.count() - count_set_intersection(fluent_atoms, negative_fluent_goals);
    score += count_set_intersection(derived_atoms, positive_derived_goals);
    score += negative_derived_goals.count() - count_set_intersection(derived_atoms, negative_derived_goals);
    return static_cast<ContinuousCost>(score);
}

bool GoalCountLayerOrderingStrategyImpl::prefer_higher_scores() const { return m_prefer_more_satisfied_goals; }

size_t GoalCountLayerOrderingStrategyImpl::count_satisfied_goal_literals(const State& state) const
{
    const auto& fluent_atoms = state.get_atoms<FluentTag>();
    const auto& derived_atoms = state.get_atoms<DerivedTag>();

    const auto& positive_fluent_goals = m_problem->get_goal_atoms_bitset<PositiveTag, FluentTag>();
    const auto& negative_fluent_goals = m_problem->get_goal_atoms_bitset<NegativeTag, FluentTag>();
    const auto& positive_derived_goals = m_problem->get_goal_atoms_bitset<PositiveTag, DerivedTag>();
    const auto& negative_derived_goals = m_problem->get_goal_atoms_bitset<NegativeTag, DerivedTag>();

    size_t score = 0;
    score += count_set_intersection(fluent_atoms, positive_fluent_goals);
    score += negative_fluent_goals.count() - count_set_intersection(fluent_atoms, negative_fluent_goals);
    score += count_set_intersection(derived_atoms, positive_derived_goals);
    score += negative_derived_goals.count() - count_set_intersection(derived_atoms, negative_derived_goals);
    return score;
}

void GoalCountLayerOrderingStrategyImpl::order_layer(StateList& states, DiscreteCost g_value)
{
    [[maybe_unused]] auto ignored_g_value = g_value;

    auto scored_states = std::vector<std::pair<State, size_t>> {};
    scored_states.reserve(states.size());
    for (const auto& state : states)
    {
        scored_states.emplace_back(state, count_satisfied_goal_literals(state));
    }

    std::ranges::stable_sort(scored_states,
                             [this](const auto& lhs, const auto& rhs)
                             { return m_prefer_more_satisfied_goals ? (lhs.second > rhs.second) : (lhs.second < rhs.second); });

    for (size_t i = 0; i < scored_states.size(); ++i)
    {
        states[i] = std::move(scored_states[i].first);
    }
}

GoalCountLayerOrderingStrategy GoalCountLayerOrderingStrategyImpl::create(formalism::Problem problem, bool prefer_more_satisfied_goals)
{ return std::make_shared<GoalCountLayerOrderingStrategyImpl>(std::move(problem), prefer_more_satisfied_goals); }

}
