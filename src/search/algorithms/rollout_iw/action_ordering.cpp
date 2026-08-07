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

#include "mimir/search/algorithms/rollout_iw/action_ordering.hpp"

#include "mimir/formalism/action.hpp"
#include "mimir/formalism/conjunctive_condition.hpp"
#include "mimir/formalism/domain.hpp"
#include "mimir/formalism/effects.hpp"
#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/ground_conjunctive_condition.hpp"
#include "mimir/formalism/ground_effects.hpp"
#include "mimir/formalism/literal.hpp"
#include "mimir/formalism/predicate.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/search/state.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

using namespace mimir::formalism;

namespace mimir::search::rollout_iw
{

namespace
{

constexpr uint32_t INF_RANK = std::numeric_limits<uint32_t>::max();

/// @brief Fill `out_order` with `0, 1, ... n-1`.
void identity_order(size_t n, std::vector<uint32_t>& out_order)
{
    out_order.resize(n);
    std::iota(out_order.begin(), out_order.end(), uint32_t(0));
}

/// @brief Stable sort of an identity permutation by `score`, lowest first.
///
/// Stable so that equal scores preserve the generator's order: two strategies that assign the same
/// scores then produce the same permutation, which is what makes fixed-seed runs reproducible.
void order_by_score(const std::vector<uint32_t>& score, std::vector<uint32_t>& out_order)
{
    identity_order(score.size(), out_order);
    std::stable_sort(out_order.begin(), out_order.end(), [&](uint32_t lhs, uint32_t rhs) { return score[lhs] < score[rhs]; });
}

class InOrderStrategy : public IActionOrderingStrategy
{
public:
    void rank(const State&, const std::vector<GroundAction>& actions, std::vector<uint32_t>& out_order) override { identity_order(actions.size(), out_order); }
};

class RandomizedStrategy : public IActionOrderingStrategy
{
private:
    std::mt19937_64 m_rng;

public:
    explicit RandomizedStrategy(uint64_t seed) : m_rng(seed) {}

    void rank(const State&, const std::vector<GroundAction>& actions, std::vector<uint32_t>& out_order) override
    {
        identity_order(actions.size(), out_order);
        std::shuffle(out_order.begin(), out_order.end(), m_rng);
    }
};

/// @brief The positive fluent atoms an action adds, across all of its conditional effects.
///
/// Conditional effects whose own condition does not currently hold are included. That is
/// deliberate: this drives ordering only, and checking each condition against the state would cost
/// more than it saves. An action that *might* add a goal atom is worth trying early.
template<typename Callback>
void for_each_added_fluent_atom(GroundAction action, Callback&& callback)
{
    for (const auto& conditional_effect : action->get_conditional_effects())
    {
        for (const auto atom_index : conditional_effect->get_conjunctive_effect()->get_compressed_propositional_effects<PositiveTag>().compressed_range())
        {
            callback(atom_index);
        }
    }
}

/// @brief Ranks actions that add one of the goal's positive fluent atoms ahead of the rest.
///
/// One implementation covers grounded and lifted mode. The strategy only ever sees ground actions
/// -- the rollout materializes a node's applicable actions before asking for an order -- so their
/// effects can be inspected directly, and unifying the goal atom against the schema's effect
/// literals instead would compute the same partition more indirectly.
class DirectGoalAchieverFirstStrategy : public IActionOrderingStrategy
{
private:
    std::vector<uint8_t> m_is_goal_atom;  ///< indexed by fluent atom index
    std::vector<uint32_t> m_score;

public:
    DirectGoalAchieverFirstStrategy(GroundConjunctiveCondition goal)
    {
        for (const auto atom_index : goal->get_compressed_precondition<PositiveTag, FluentTag>()->compressed_range())
        {
            if (atom_index >= m_is_goal_atom.size())
            {
                m_is_goal_atom.resize(atom_index + 1, 0);
            }
            m_is_goal_atom[atom_index] = 1;
        }
    }

    void rank(const State&, const std::vector<GroundAction>& actions, std::vector<uint32_t>& out_order) override
    {
        m_score.assign(actions.size(), 1);
        for (size_t i = 0; i < actions.size(); ++i)
        {
            for_each_added_fluent_atom(actions[i],
                                       [&](Index atom_index)
                                       {
                                           if ((atom_index < m_is_goal_atom.size()) && m_is_goal_atom[atom_index])
                                           {
                                               m_score[i] = 0;
                                           }
                                       });
        }
        order_by_score(m_score, out_order);
    }
};

/// @brief Per-action-schema distance to the goal under a coarse regression over predicates.
///
/// Computed once, at the schema level, from the domain alone: a predicate that appears in the goal
/// has rank 0; a predicate appearing in the precondition of a schema that can add a rank-r
/// predicate has rank r+1; and a schema scores the best rank any of its add effects touches. It
/// ignores objects entirely, so it is cheap enough to build per worker and stays valid however much
/// the ground atom universe grows during a lifted search.
std::vector<uint32_t> compute_schema_regression_ranks(const ProblemImpl& problem, GroundConjunctiveCondition goal)
{
    const auto& actions = problem.get_domain()->get_actions();

    auto predicate_rank = std::vector<uint32_t> {};
    const auto set_predicate_rank = [&](Index predicate_index, uint32_t rank)
    {
        if (predicate_index >= predicate_rank.size())
        {
            predicate_rank.resize(predicate_index + 1, INF_RANK);
        }
        if (rank < predicate_rank[predicate_index])
        {
            predicate_rank[predicate_index] = rank;
            return true;
        }
        return false;
    };
    const auto get_predicate_rank = [&](Index predicate_index)
    { return (predicate_index < predicate_rank.size()) ? predicate_rank[predicate_index] : INF_RANK; };

    /* Seed: every fluent predicate mentioned positively in the goal. */
    for (const auto atom_index : goal->get_compressed_precondition<PositiveTag, FluentTag>()->compressed_range())
    {
        set_predicate_rank(problem.get_repositories().get_ground_atom<FluentTag>(atom_index)->get_predicate()->get_index(), 0);
    }

    /* Regress to a fixpoint. Each pass can only lower ranks, and a rank is bounded by the number of
       predicates, so the outer loop terminates; the `changed` guard usually ends it far sooner. */
    for (size_t pass = 0; pass <= actions.size(); ++pass)
    {
        auto changed = false;

        for (const auto& action : actions)
        {
            auto best_effect_rank = INF_RANK;
            for (const auto& conditional_effect : action->get_conditional_effects())
            {
                for (const auto& literal : conditional_effect->get_conjunctive_effect()->get_literals())
                {
                    if (literal->get_polarity())
                    {
                        best_effect_rank = std::min(best_effect_rank, get_predicate_rank(literal->get_atom()->get_predicate()->get_index()));
                    }
                }
            }

            if (best_effect_rank == INF_RANK)
            {
                continue;  ///< this schema cannot (yet) contribute to anything goal-relevant
            }

            for (const auto& literal : action->get_conjunctive_condition()->get_literals<FluentTag>())
            {
                changed |= set_predicate_rank(literal->get_atom()->get_predicate()->get_index(), best_effect_rank + 1);
            }
        }

        if (!changed)
        {
            break;
        }
    }

    /* Project onto schemas: a schema is as relevant as the best-ranked predicate it can add. */
    auto schema_rank = std::vector<uint32_t> {};
    for (const auto& action : actions)
    {
        const auto action_index = action->get_index();
        if (action_index >= schema_rank.size())
        {
            schema_rank.resize(action_index + 1, INF_RANK);
        }

        auto best = INF_RANK;
        for (const auto& conditional_effect : action->get_conditional_effects())
        {
            for (const auto& literal : conditional_effect->get_conjunctive_effect()->get_literals())
            {
                if (literal->get_polarity())
                {
                    best = std::min(best, get_predicate_rank(literal->get_atom()->get_predicate()->get_index()));
                }
            }
        }
        schema_rank[action_index] = best;
    }

    return schema_rank;
}

class GoalRegressionRelevanceStrategy : public IActionOrderingStrategy
{
private:
    std::vector<uint32_t> m_schema_rank;
    std::vector<uint32_t> m_score;

public:
    GoalRegressionRelevanceStrategy(const ProblemImpl& problem, GroundConjunctiveCondition goal) :
        m_schema_rank(compute_schema_regression_ranks(problem, goal))
    {
    }

    void rank(const State&, const std::vector<GroundAction>& actions, std::vector<uint32_t>& out_order) override
    {
        m_score.resize(actions.size());
        for (size_t i = 0; i < actions.size(); ++i)
        {
            const auto action_index = actions[i]->get_action()->get_index();
            m_score[i] = (action_index < m_schema_rank.size()) ? m_schema_rank[action_index] : INF_RANK;
        }
        order_by_score(m_score, out_order);
    }
};

/// @brief Regression ranks, but ties broken at random instead of by generator order.
///
/// This is the diversification knob for a portfolio: several workers can share the same guidance
/// while exploring different actions among the many that the coarse ranking calls equally good.
class MixedRegressionRandomStrategy : public IActionOrderingStrategy
{
private:
    std::vector<uint32_t> m_schema_rank;
    std::vector<uint32_t> m_score;
    std::mt19937_64 m_rng;

public:
    MixedRegressionRandomStrategy(const ProblemImpl& problem, GroundConjunctiveCondition goal, uint64_t seed) :
        m_schema_rank(compute_schema_regression_ranks(problem, goal)),
        m_rng(seed)
    {
    }

    void rank(const State&, const std::vector<GroundAction>& actions, std::vector<uint32_t>& out_order) override
    {
        m_score.resize(actions.size());
        for (size_t i = 0; i < actions.size(); ++i)
        {
            const auto action_index = actions[i]->get_action()->get_index();
            m_score[i] = (action_index < m_schema_rank.size()) ? m_schema_rank[action_index] : INF_RANK;
        }

        /* Shuffle first, then sort stably by rank: the shuffle decides the order within each rank
           and the stable sort preserves it. */
        identity_order(actions.size(), out_order);
        std::shuffle(out_order.begin(), out_order.end(), m_rng);
        std::stable_sort(out_order.begin(), out_order.end(), [&](uint32_t lhs, uint32_t rhs) { return m_score[lhs] < m_score[rhs]; });
    }
};

}

ActionOrderingStrategy create_action_ordering_strategy(const ActionOrderingConfiguration& configuration,
                                                      const Problem& problem,
                                                      std::optional<GroundConjunctiveCondition> goal)
{
    if (!problem)
    {
        throw std::runtime_error("rollout_iw::create_action_ordering_strategy: problem must not be null.");
    }

    switch (configuration.kind)
    {
        case ActionOrderingKind::IN_ORDER:
            return std::make_shared<InOrderStrategy>();
        case ActionOrderingKind::RANDOMIZED:
            return std::make_shared<RandomizedStrategy>(configuration.seed);
        case ActionOrderingKind::DIRECT_GOAL_ACHIEVER_FIRST:
            return std::make_shared<DirectGoalAchieverFirstStrategy>(goal ? *goal : problem->get_goal_condition());
        case ActionOrderingKind::GOAL_REGRESSION_RELEVANCE:
            return std::make_shared<GoalRegressionRelevanceStrategy>(*problem, goal ? *goal : problem->get_goal_condition());
        case ActionOrderingKind::MIXED_REGRESSION_RANDOM:
            return std::make_shared<MixedRegressionRandomStrategy>(*problem, goal ? *goal : problem->get_goal_condition(), configuration.seed);
    }

    throw std::runtime_error("rollout_iw::create_action_ordering_strategy: unknown ActionOrderingKind.");
}

}
