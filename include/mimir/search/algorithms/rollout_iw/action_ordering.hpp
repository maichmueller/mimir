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

#ifndef MIMIR_SEARCH_ALGORITHMS_ROLLOUT_IW_ACTION_ORDERING_HPP_
#define MIMIR_SEARCH_ALGORITHMS_ROLLOUT_IW_ACTION_ORDERING_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/search/declarations.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace mimir::search::rollout_iw
{

/// @brief Decides which applicable action a rollout tries first at a node.
///
/// This is guidance, never pruning. `rank` must write a permutation of `[0, actions.size())` into
/// `out_order`: every applicable action has to remain reachable, or SOLVED propagation stops being
/// exact and the algorithm loses its "no more children" guarantee. A hostile ordering may make the
/// search slow; it must never make it incomplete.
class IActionOrderingStrategy
{
public:
    virtual ~IActionOrderingStrategy() = default;

    /// @brief Write a permutation of the indices of `actions`, best candidate first.
    virtual void rank(const State& state, const std::vector<formalism::GroundAction>& actions, std::vector<uint32_t>& out_order) = 0;
};

using ActionOrderingStrategy = std::shared_ptr<IActionOrderingStrategy>;

enum class ActionOrderingKind
{
    /// @brief Whatever order the applicable-action generator produced.
    IN_ORDER,
    /// @brief Seeded shuffle. The cheapest way to make K rollout workers explore differently.
    RANDOMIZED,
    /// @brief Actions that add a goal atom first, everything else after, each group in generator order.
    DIRECT_GOAL_ACHIEVER_FIRST,
    /// @brief Actions ordered by how few regression steps separate their add effects from the goal.
    GOAL_REGRESSION_RELEVANCE,
    /// @brief `GOAL_REGRESSION_RELEVANCE` with seeded random tie-breaking inside each rank.
    MIXED_REGRESSION_RANDOM,
};

struct ActionOrderingConfiguration
{
    ActionOrderingKind kind = ActionOrderingKind::IN_ORDER;
    uint64_t seed = 0;

    ActionOrderingConfiguration() = default;
    explicit ActionOrderingConfiguration(ActionOrderingKind kind, uint64_t seed = 0) : kind(kind), seed(seed) {}
};

/// @brief Build the strategy described by `configuration`.
///
/// The goal-directed kinds need to know what they are aiming at: pass the condition the search
/// actually tests. When `goal` is null the problem's own goal condition is used.
///
/// Every strategy instance owns mutable state (an RNG, scratch vectors) and must not be shared
/// across threads: build one per search worker.
ActionOrderingStrategy create_action_ordering_strategy(const ActionOrderingConfiguration& configuration,
                                                       const formalism::Problem& problem,
                                                       std::optional<formalism::GroundConjunctiveCondition> goal = std::nullopt);

}

#endif
