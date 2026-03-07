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

#ifndef MIMIR_SEARCH_ALGORITHMS_STRATEGIES_LAYER_ORDERING_STRATEGY_HPP_
#define MIMIR_SEARCH_ALGORITHMS_STRATEGIES_LAYER_ORDERING_STRATEGY_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/search/state.hpp"

#include <cstdint>
#include <optional>
#include <random>

namespace mimir::search
{

/// @brief `ILayerOrderingStrategy` encapsulates logic to reorder a frontier layer before it is expanded.
class ILayerOrderingStrategy
{
public:
    virtual ~ILayerOrderingStrategy() = default;

    virtual void order_layer(StateList& states, DiscreteCost g_value) = 0;
};

class InOrderLayerOrderingStrategyImpl : public ILayerOrderingStrategy
{
public:
    void order_layer(StateList& states, DiscreteCost g_value) override;

    static InOrderLayerOrderingStrategy create();
};

class ReverseOrderLayerOrderingStrategyImpl : public ILayerOrderingStrategy
{
public:
    void order_layer(StateList& states, DiscreteCost g_value) override;

    static ReverseOrderLayerOrderingStrategy create();
};

class RandomizedLayerOrderingStrategyImpl : public ILayerOrderingStrategy
{
    std::mt19937_64 m_rng;

public:
    explicit RandomizedLayerOrderingStrategyImpl(std::optional<uint64_t> seed = std::nullopt);

    void order_layer(StateList& states, DiscreteCost g_value) override;

    static RandomizedLayerOrderingStrategy create(std::optional<uint64_t> seed = std::nullopt);
};

class GoalCountLayerOrderingStrategyImpl : public ILayerOrderingStrategy
{
    formalism::Problem m_problem;
    bool m_prefer_more_satisfied_goals;

    size_t count_satisfied_goal_literals(const State& state) const;

public:
    explicit GoalCountLayerOrderingStrategyImpl(formalism::Problem problem, bool prefer_more_satisfied_goals = true);

    void order_layer(StateList& states, DiscreteCost g_value) override;

    static GoalCountLayerOrderingStrategy create(formalism::Problem problem, bool prefer_more_satisfied_goals = true);
};

}

#endif
