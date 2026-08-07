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

/// Gate for T3/T4 of docs/ROLLOUT_IW_IMPLEMENTATION_PLAN.md: true Rollout IW(1) after
/// Bandres, Bonet and Geffner, and the action-ordering strategies that guide it.

#include "mimir/search/algorithms/rollout_iw.hpp"

#include "mimir/formalism/action.hpp"
#include "mimir/formalism/domain.hpp"
#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/object.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/search/algorithms/brfs.hpp"
#include "mimir/search/algorithms/strategies/goal_strategy.hpp"
#include "mimir/search/applicability.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

namespace mimir::tests
{

using namespace mimir::formalism;
using namespace mimir::search;

namespace
{

/// A domain small enough that a whole Rollout IW run can be traced by hand.
///
/// Two always-applicable actions over one object toggle the fluent atoms `(p o)` and `(q o)` on,
/// and neither can ever be turned off again, so the reachable state space is the four subsets of
/// {p, q}. `mk-r` exists only to make `r` a fluent predicate rather than a static one -- its
/// precondition `blocked` is static and absent from the initial state, so it is never applicable
/// and the goal `(r o)` is unreachable. The search therefore runs to exhaustion, which is what
/// makes every case count observable. `dummy` is a static atom that holds, used as the "empty"
/// precondition so the test does not depend on how the parser treats `(and)`.
constexpr const char* kTinyDomain = R"(
(define (domain rolloutiw-tiny)
 (:requirements :strips :typing)
 (:types thing)
 (:predicates (p ?x - thing) (q ?x - thing) (r ?x - thing) (dummy ?x - thing) (blocked ?x - thing))
 (:action mk-p :parameters (?x - thing) :precondition (and (dummy ?x)) :effect (and (p ?x)))
 (:action mk-q :parameters (?x - thing) :precondition (and (dummy ?x)) :effect (and (q ?x)))
 (:action mk-r :parameters (?x - thing) :precondition (and (blocked ?x)) :effect (and (r ?x)))
)
)";

constexpr const char* kTinyProblem = R"(
(define (problem rolloutiw-tiny-p1)
 (:domain rolloutiw-tiny)
 (:objects o - thing)
 (:init (dummy o))
 (:goal (and (r o)))
)
)";

/// A chain of locations with exactly one plan, of length 3. Width 1 by construction -- each step
/// makes a brand-new atom true -- so Rollout IW finds it in a single rollout under any ordering,
/// which makes it the right place to pin down depth-bound behaviour exactly.
constexpr const char* kChainDomain = R"(
(define (domain rolloutiw-chain)
 (:requirements :strips :typing)
 (:types loc)
 (:predicates (at ?x - loc) (succ ?x ?y - loc))
 (:action move :parameters (?from ?to - loc) :precondition (and (at ?from) (succ ?from ?to)) :effect (and (not (at ?from)) (at ?to)))
)
)";

constexpr const char* kChainProblem = R"(
(define (problem rolloutiw-chain-p1)
 (:domain rolloutiw-chain)
 (:objects l0 l1 l2 l3 - loc)
 (:init (at l0) (succ l0 l1) (succ l1 l2) (succ l2 l3))
 (:goal (and (at l3)))
)
)";

/// The same chain, cut down to a single step. A depth-1 goal is the one case the incumbent bound
/// cannot prune, which is what the restart loop in the atomic-goal portfolio has to reckon with.
constexpr const char* kOneStepChainProblem = R"(
(define (problem rolloutiw-chain-one-step)
 (:domain rolloutiw-chain)
 (:objects l0 l1 - loc)
 (:init (at l0) (succ l0 l1))
 (:goal (and (at l1)))
)
)";

Problem parse_inline_problem(const char* domain, const char* problem)
{
    return ProblemImpl::create(domain, fs::path("rolloutiw-tiny-domain.pddl"), problem, fs::path("rolloutiw-tiny-problem.pddl"));
}

SearchContext create_grounded_context(const Problem& problem)
{
    return SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
}

SearchContext create_grounded_context(const std::string& domain, const std::string& problem)
{
    return SearchContextImpl::create(fs::path(std::string(DATA_DIR) + domain),
                                     fs::path(std::string(DATA_DIR) + problem),
                                     SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
}

SearchContext create_lifted_context(const std::string& domain, const std::string& problem)
{
    return SearchContextImpl::create(
        fs::path(std::string(DATA_DIR) + domain),
        fs::path(std::string(DATA_DIR) + problem),
        SearchContextImpl::Options(SearchContextImpl::LiftedOptions(SearchContextImpl::LiftedOptions::KPKCOptions(SearchContextImpl::SymmetryPruning::OFF))));
}

/// An ordering driven by a script of schema-name priorities, one entry per `rank` call.
///
/// A strategy that is a fixed function of the node makes the rollouts a plain depth-first traversal,
/// under which case 3 provably cannot occur: a node is only revisited while it is still OPEN, and
/// everything created since then is one of its own descendants and therefore deeper, so nothing can
/// have taken over a feature it holds at its own best depth. Letting the order vary between
/// rollouts -- which is what randomized or policy-driven orderings do in practice -- is what
/// exposes case 3, and scripting the variation keeps the trace hand-derivable.
class ScriptedOrdering : public rollout_iw::IActionOrderingStrategy
{
private:
    std::vector<std::vector<std::string>> m_script;
    std::vector<std::string> m_default_priority;
    size_t m_calls = 0;

public:
    ScriptedOrdering(std::vector<std::vector<std::string>> script, std::vector<std::string> default_priority) :
        m_script(std::move(script)),
        m_default_priority(std::move(default_priority))
    {
    }

    size_t get_num_calls() const { return m_calls; }

    void rank(const State&, const std::vector<GroundAction>& actions, std::vector<uint32_t>& out_order) override
    {
        const auto& priority = (m_calls < m_script.size()) ? m_script[m_calls] : m_default_priority;
        ++m_calls;

        const auto rank_of = [&](uint32_t position)
        {
            const auto& name = actions[position]->get_action()->get_name();
            const auto it = std::find(priority.begin(), priority.end(), name);
            return static_cast<size_t>(it - priority.begin());
        };

        out_order.resize(actions.size());
        std::iota(out_order.begin(), out_order.end(), uint32_t(0));
        std::stable_sort(out_order.begin(), out_order.end(), [&](uint32_t lhs, uint32_t rhs) { return rank_of(lhs) < rank_of(rhs); });
    }
};

/// The exact inverse of goal-directed guidance: anything that adds a goal atom is tried last.
/// Used to show that ordering only reorders -- it never removes an action from consideration.
class HostileOrdering : public rollout_iw::IActionOrderingStrategy
{
private:
    std::vector<uint8_t> m_is_goal_atom;

public:
    explicit HostileOrdering(GroundConjunctiveCondition goal)
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
        auto score = std::vector<uint32_t>(actions.size(), 0);
        for (size_t i = 0; i < actions.size(); ++i)
        {
            for (const auto& conditional_effect : actions[i]->get_conditional_effects())
            {
                for (const auto atom_index :
                     conditional_effect->get_conjunctive_effect()->get_compressed_propositional_effects<PositiveTag>().compressed_range())
                {
                    if ((atom_index < m_is_goal_atom.size()) && m_is_goal_atom[atom_index])
                    {
                        score[i] = 1;  ///< achieves a goal atom, so: last
                    }
                }
            }
        }

        out_order.resize(actions.size());
        std::iota(out_order.begin(), out_order.end(), uint32_t(0));
        std::stable_sort(out_order.begin(), out_order.end(), [&](uint32_t lhs, uint32_t rhs) { return score[lhs] > score[rhs]; });
    }
};

/// A total order on ground actions that does not depend on how the generator enumerated them:
/// schema name first, then the binding's object names. Two search contexts over the same instance
/// therefore descend through exactly the same actions in exactly the same order, whatever the
/// generator is -- which is what turns a grounded-vs-lifted comparison into a real parity check
/// rather than a comparison of two different traversals.
class CanonicalOrdering : public rollout_iw::IActionOrderingStrategy
{
public:
    void rank(const State&, const std::vector<GroundAction>& actions, std::vector<uint32_t>& out_order) override
    {
        const auto key = [&](uint32_t position)
        {
            auto result = std::string(actions[position]->get_action()->get_name());
            for (const auto& object : actions[position]->get_objects())
            {
                result += ' ';
                result += object->get_name();
            }
            return result;
        };

        out_order.resize(actions.size());
        std::iota(out_order.begin(), out_order.end(), uint32_t(0));
        std::stable_sort(out_order.begin(), out_order.end(), [&](uint32_t lhs, uint32_t rhs) { return key(lhs) < key(rhs); });
    }
};

/// Replay `plan_steps` from the initial state by re-grounding each schema/binding pair, asserting
/// applicability at every step, and return whether the final state satisfies the goal. This is the
/// check that matters for the portfolio: the schema/binding form is what crosses a thread boundary,
/// and it is only useful if it can be turned back into a real plan.
bool replay_plan_steps(const SearchContext& context, const rollout_iw::PlanStepList& plan_steps)
{
    auto& problem = *context->get_problem();
    auto& state_repository = *context->get_state_repository();
    auto goal_strategy = ProblemGoalStrategyImpl::create(context->get_problem());

    auto [state, metric_value] = state_repository.get_or_create_initial_state();

    for (const auto& step : plan_steps)
    {
        const auto action = problem.ground(step.schema, step.binding);
        if (!is_applicable(action, state))
        {
            return false;
        }
        const auto successor = state_repository.get_or_create_successor_state(state, action, metric_value);
        state = successor.first;
        metric_value = successor.second;
    }

    return goal_strategy->test_dynamic_goal(state);
}

}

/// Exact four-case accounting. Every number below is read off a hand-derived trace of the tiny
/// domain under the scripted ordering; see the comment block for the trace itself.
///
///  R1  #1 n0{}      [A,B]  A untried -> n1{p}@1    p:inf->1   CASE 1, descend
///      #2 n1{p}     [B,A]  B untried -> n2{p,q}@2  q:inf->2   CASE 1, descend
///      #3 n2{p,q}   [A,B]  A untried -> n3{p,q}@3  no gain    CASE 2, solved, end
///  R2  #4 n0        [B,A]  B untried -> n4{q}@1    q:2->1     CASE 1, descend
///      #5 n4{q}     [A,B]  A untried -> n5{p,q}@2  no gain    CASE 2, solved, end
///  R3  #6 n0        [A,B]  A -> n1 OPEN, p still at depth 1   CASE 4, descend
///      #7 n1        [A,B]  A untried -> n6{p}@2    no gain    CASE 2, solved, end
///  R4  #8 n0        [A,B]  A -> n1 OPEN                       CASE 4, descend
///      #9 n1        [A,B]  A solved; B -> n2, whose q was
///                          taken over by n4 at depth 1        CASE 3, solved
///                          -> n1's children all solved: n1 solved (1 propagation), end
///  R5 #10 n0        [A,B]  A solved; B -> n4 OPEN, q at 1     CASE 4, descend
///      #11 n4       [A,B]  A solved; B untried -> n7{q}@2     CASE 2, solved
///                          -> n4 solved, then n0 solved (2 more propagations), end
TEST(MimirTests, SearchAlgorithmsRolloutIWExactFourCaseAccountingTest)
{
    const auto context = create_grounded_context(parse_inline_problem(kTinyDomain, kTinyProblem));

    auto ordering = std::make_shared<ScriptedOrdering>(
        std::vector<std::vector<std::string>> { { "mk-p", "mk-q" }, { "mk-q", "mk-p" }, { "mk-p", "mk-q" }, { "mk-q", "mk-p" } },
        std::vector<std::string> { "mk-p", "mk-q" });

    auto options = rollout_iw::Options();
    options.action_ordering = ordering;

    const auto result = rollout_iw::find_solution(context, options);

    EXPECT_EQ(result.search_result.status, SearchStatus::EXHAUSTED);
    EXPECT_TRUE(result.root_solved);

    EXPECT_EQ(result.statistics.num_case_1, 3u);
    EXPECT_EQ(result.statistics.num_case_2, 4u);
    EXPECT_EQ(result.statistics.num_case_3, 1u);
    EXPECT_EQ(result.statistics.num_case_4, 3u);

    EXPECT_EQ(result.statistics.num_rollouts, 5u);
    EXPECT_EQ(result.statistics.num_generated_states, 7u);
    EXPECT_EQ(result.statistics.num_tree_nodes, 8u);
    EXPECT_EQ(result.statistics.num_expanded_nodes, 4u);  ///< n0, n1, n2, n4 -- the descended-into nodes
    EXPECT_EQ(result.statistics.num_solved_propagations, 3u);
    EXPECT_EQ(result.statistics.num_feature_depth_improvements, 3u);
    EXPECT_EQ(result.statistics.max_rollout_depth, 3u);
    EXPECT_EQ(result.statistics.num_dead_ends, 0u);
    EXPECT_EQ(ordering->get_num_calls(), 11u);
}

/// A node becomes SOLVED only once *every* one of its children is SOLVED. After the first rollout
/// of the trace above, n2 has had one of its two children generated and solved, so nothing may
/// propagate and the root must still be OPEN.
TEST(MimirTests, SearchAlgorithmsRolloutIWSolvedRequiresEveryChildTest)
{
    const auto context = create_grounded_context(parse_inline_problem(kTinyDomain, kTinyProblem));

    auto options = rollout_iw::Options();
    options.action_ordering = std::make_shared<ScriptedOrdering>(std::vector<std::vector<std::string>> {}, std::vector<std::string> { "mk-p", "mk-q" });
    options.max_rollouts = 1;

    const auto result = rollout_iw::find_solution(context, options);

    EXPECT_FALSE(result.root_solved);
    EXPECT_EQ(result.statistics.num_rollouts, 1u);
    EXPECT_EQ(result.statistics.num_solved_propagations, 0u);
    EXPECT_EQ(result.search_result.status, SearchStatus::FAILED);
    EXPECT_EQ(result.stop_reason, "rollout budget expired");
    EXPECT_TRUE(result.plan_steps.empty());
}

/// Ordering is guidance, not pruning. An ordering that deliberately ranks every goal-achieving
/// action last -- the exact inverse of what the goal-directed strategies do -- must still find a
/// valid plan, because the action it demoted is still in the permutation.
///
/// It need not find the *shortest* plan: Rollout IW is not an optimal search, and which plan it
/// reaches depends on the order it descends in. The chain instance, whose single width-1 path is
/// the only plan there is, pins down the case where hostility cannot be routed around.
TEST(MimirTests, SearchAlgorithmsRolloutIWHostileOrderingStillSolvesTest)
{
    {
        const auto context = create_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

        auto options = rollout_iw::Options();
        options.action_ordering = std::make_shared<HostileOrdering>(context->get_problem()->get_goal_condition());

        const auto result = rollout_iw::find_solution(context, options);

        ASSERT_EQ(result.search_result.status, SearchStatus::SOLVED);
        EXPECT_EQ(result.plan_steps.size(), result.plan_length);
        EXPECT_TRUE(replay_plan_steps(context, result.plan_steps));
    }

    {
        const auto context = create_grounded_context(parse_inline_problem(kChainDomain, kChainProblem));

        auto options = rollout_iw::Options();
        options.action_ordering = std::make_shared<HostileOrdering>(context->get_problem()->get_goal_condition());

        const auto result = rollout_iw::find_solution(context, options);

        ASSERT_EQ(result.search_result.status, SearchStatus::SOLVED);
        EXPECT_EQ(result.plan_length, 3u);
        EXPECT_TRUE(replay_plan_steps(context, result.plan_steps));
    }
}

namespace
{
const std::vector<rollout_iw::ActionOrderingKind> kBuiltinOrderingKinds = { rollout_iw::ActionOrderingKind::IN_ORDER,
                                                                           rollout_iw::ActionOrderingKind::RANDOMIZED,
                                                                           rollout_iw::ActionOrderingKind::DIRECT_GOAL_ACHIEVER_FIRST,
                                                                           rollout_iw::ActionOrderingKind::GOAL_REGRESSION_RELEVANCE,
                                                                           rollout_iw::ActionOrderingKind::MIXED_REGRESSION_RANDOM };
}

/// No built-in ordering may cost the search a solution it would otherwise find, and none may produce
/// a plan that does not replay.
///
/// The must-solve half runs on the chain instance, which is width 1 and therefore inside Rollout
/// IW(1)'s guarantee. Gripper is not: its conjunctive goal has width 2, so whether a width-1 search
/// stumbles onto a plan there depends on the order it happens to descend in -- which, given that
/// Mimir's grounded generator does not enumerate actions reproducibly (see the portfolio tests),
/// makes "must solve gripper" an assertion about luck. What is asserted there instead is that a plan,
/// if returned, is real.
TEST(MimirTests, SearchAlgorithmsRolloutIWBuiltinOrderingsSolveTest)
{
    for (const auto kind : kBuiltinOrderingKinds)
    {
        {
            const auto context = create_grounded_context(parse_inline_problem(kChainDomain, kChainProblem));

            auto options = rollout_iw::Options();
            options.action_ordering_configuration = rollout_iw::ActionOrderingConfiguration(kind, 17);

            const auto result = rollout_iw::find_solution(context, options);

            ASSERT_EQ(result.search_result.status, SearchStatus::SOLVED) << "ordering kind " << static_cast<int>(kind);
            EXPECT_EQ(result.plan_length, 3u) << "ordering kind " << static_cast<int>(kind);
            EXPECT_TRUE(replay_plan_steps(context, result.plan_steps)) << "ordering kind " << static_cast<int>(kind);
        }

        {
            const auto context = create_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

            auto options = rollout_iw::Options();
            options.action_ordering_configuration = rollout_iw::ActionOrderingConfiguration(kind, 17);

            const auto result = rollout_iw::find_solution(context, options);

            if (result.search_result.status == SearchStatus::SOLVED)
            {
                EXPECT_TRUE(replay_plan_steps(context, result.plan_steps)) << "ordering kind " << static_cast<int>(kind);
                EXPECT_EQ(result.plan_steps.size(), result.plan_length) << "ordering kind " << static_cast<int>(kind);
            }
        }
    }
}

/// Fixed seeds must give a reproducible result, or the portfolio cannot be tested at all.
///
/// On the chain instance, where exactly one action is applicable per state and the generator's own
/// unreproducible enumeration order therefore cannot leak into the answer.
TEST(MimirTests, SearchAlgorithmsRolloutIWFixedSeedIsDeterministicTest)
{
    for (const auto kind : kBuiltinOrderingKinds)
    {
        const auto run = [&](uint64_t seed)
        {
            const auto context = create_grounded_context(parse_inline_problem(kChainDomain, kChainProblem));
            auto options = rollout_iw::Options();
            options.action_ordering_configuration = rollout_iw::ActionOrderingConfiguration(kind, seed);
            return rollout_iw::find_solution(context, options);
        };

        const auto first = run(4711);
        const auto second = run(4711);

        EXPECT_EQ(first.search_result.status, second.search_result.status) << static_cast<int>(kind);
        EXPECT_EQ(first.plan_length, second.plan_length) << static_cast<int>(kind);
        EXPECT_EQ(first.statistics.num_rollouts, second.statistics.num_rollouts) << static_cast<int>(kind);
        EXPECT_EQ(first.statistics.num_generated_states, second.statistics.num_generated_states) << static_cast<int>(kind);
        EXPECT_EQ(first.statistics.num_case_1, second.statistics.num_case_1) << static_cast<int>(kind);
        EXPECT_EQ(first.statistics.num_case_2, second.statistics.num_case_2) << static_cast<int>(kind);
        EXPECT_EQ(first.statistics.num_case_3, second.statistics.num_case_3) << static_cast<int>(kind);
        EXPECT_EQ(first.statistics.num_case_4, second.statistics.num_case_4) << static_cast<int>(kind);
    }
}

/// The goal is tested before the incumbent bound is applied, so a goal sitting exactly on the
/// boundary is found rather than pruned. With an incumbent of `L + 1` the depth-`L` goal is at
/// depth `(L + 1) - 1`; checking the bound first would cut its parent away.
/// The chain instance has exactly one plan, of length 3, whose goal sits at depth 3.
TEST(MimirTests, SearchAlgorithmsRolloutIWGoalTestedBeforeIncumbentBoundTest)
{
    {
        /* Incumbent 4 means "find something shorter than 4". The depth-3 goal qualifies, and its
           parent at depth 2 sits exactly on the cutoff (2 + 1 >= 4 is false, but its own child is
           the boundary case): the goal must be tested before any bound reasoning, or the plan is
           pruned away one step before it is recognized. */
        const auto context = create_grounded_context(parse_inline_problem(kChainDomain, kChainProblem));
        auto options = rollout_iw::Options();
        options.incumbent_bound = 4;

        const auto result = rollout_iw::find_solution(context, options);

        ASSERT_EQ(result.search_result.status, SearchStatus::SOLVED);
        EXPECT_EQ(result.plan_length, 3u);
        EXPECT_EQ(result.statistics.num_incumbent_bound_prunings, 0u);
    }

    {
        /* Incumbent 3 means the only plan there is cannot improve on what is already known, so the
           search must cut that branch and exhaust rather than rediscover the same length. */
        const auto context = create_grounded_context(parse_inline_problem(kChainDomain, kChainProblem));
        auto options = rollout_iw::Options();
        options.incumbent_bound = 3;

        const auto result = rollout_iw::find_solution(context, options);

        EXPECT_EQ(result.search_result.status, SearchStatus::EXHAUSTED);
        EXPECT_TRUE(result.root_solved);
        EXPECT_EQ(result.statistics.num_incumbent_bound_prunings, 1u);
        EXPECT_EQ(result.stop_reason, "width-1 space exhausted under the depth/incumbent bounds");
    }

    {
        /* The same cutoff, but published through a shared control rather than the options. */
        const auto context = create_grounded_context(parse_inline_problem(kChainDomain, kChainProblem));
        auto control = SearchControl();
        control.improve_incumbent_length(3);

        auto options = rollout_iw::Options();
        options.control = &control;

        const auto result = rollout_iw::find_solution(context, options);

        EXPECT_EQ(result.search_result.status, SearchStatus::EXHAUSTED);
        EXPECT_EQ(result.statistics.num_incumbent_bound_prunings, 1u);
    }
}

/// A goal one step from the start is immune to the incumbent bound, however tight the bound is.
///
/// Two things combine to make this true, and both are deliberate: the goal is tested before any
/// bound reasoning, and the bound prunes a node by what its *children* would cost, which never
/// applies to the root's own children. So a caller holding a length-1 incumbent gets that very same
/// plan back, forever, from every repeat call.
///
/// That is not a defect here -- returning the plan is more useful than reporting an exhausted
/// space -- but anything that loops on "search again under the new bound" has to notice that the
/// bound did not move. `iw::find_atomic_goal_portfolio`'s rollout workers do; this test is what
/// stops that assumption from being quietly invalidated.
TEST(MimirTests, SearchAlgorithmsRolloutIWDepthOneGoalEscapesIncumbentBoundTest)
{
    for (const auto bound : { 1u, 2u })
    {
        const auto context = create_grounded_context(parse_inline_problem(kChainDomain, kOneStepChainProblem));
        auto options = rollout_iw::Options();
        options.incumbent_bound = bound;

        const auto result = rollout_iw::find_solution(context, options);

        ASSERT_EQ(result.search_result.status, SearchStatus::SOLVED) << "bound " << bound;
        EXPECT_EQ(result.plan_length, 1u) << "bound " << bound;
        EXPECT_EQ(result.statistics.num_incumbent_bound_prunings, 0u) << "bound " << bound;
    }
}

/// A start state from a sibling repository of the same problem must be rejected.
///
/// Matching problems is not a sufficient check: a repository built with `PrivateInterningTables`
/// has its own valla tables, so the state's packed indices decode to different atoms here. Nothing
/// downstream would notice -- the search would just quietly explore a state nobody asked for.
TEST(MimirTests, SearchAlgorithmsRolloutIWRejectsForeignRepositoryStartStateTest)
{
    const auto problem = parse_inline_problem(kChainDomain, kChainProblem);
    const auto context = create_grounded_context(problem);

    const auto private_repository =
        StateRepositoryImpl::create(context->get_state_repository()->get_axiom_evaluator(), StateRepositoryImpl::PrivateInterningTables {});
    const auto private_context = SearchContextImpl::create(context->get_problem(), context->get_applicable_action_generator(), private_repository);

    const auto [foreign_state, foreign_metric] = private_repository->get_or_create_initial_state();

    auto options = rollout_iw::Options();
    options.start_state = foreign_state;

    EXPECT_THROW(rollout_iw::find_solution(context, options), std::runtime_error);

    // The same state is of course fine in the context it came from.
    EXPECT_NO_THROW(rollout_iw::find_solution(private_context, options));
}

/// The same algorithm on a grounded and on a lifted KPKC context must behave identically.
///
/// Pinned down with the canonical ordering, so both runs descend through the same actions in the
/// same order and any difference is a real difference in the transition system rather than in how
/// the two generators happen to enumerate. Every counter must match, which also exercises the
/// lifted-only path where the feature-depth table grows as grounding discovers new atoms: in
/// grounded mode the whole atom universe exists up front, in lifted mode almost none of it does.
TEST(MimirTests, SearchAlgorithmsRolloutIWGroundedAndLiftedAgreeTest)
{
    const auto instances = std::vector<std::pair<std::string, std::string>> { { "gripper/domain.pddl", "gripper/test_problem.pddl" },
                                                                             { "blocks_4/domain.pddl", "blocks_4/test_problem.pddl" },
                                                                             { "spanner/domain.pddl", "spanner/test_problem.pddl" },
                                                                             { "delivery/domain.pddl", "delivery/test_problem.pddl" } };

    for (const auto& [domain, problem] : instances)
    {
        const auto grounded_context = create_grounded_context(domain, problem);
        const auto lifted_context = create_lifted_context(domain, problem);

        auto grounded_options = rollout_iw::Options();
        grounded_options.action_ordering = std::make_shared<CanonicalOrdering>();
        auto lifted_options = rollout_iw::Options();
        lifted_options.action_ordering = std::make_shared<CanonicalOrdering>();

        const auto grounded = rollout_iw::find_solution(grounded_context, grounded_options);
        const auto lifted = rollout_iw::find_solution(lifted_context, lifted_options);

        ASSERT_EQ(grounded.search_result.status, lifted.search_result.status) << domain;
        EXPECT_EQ(grounded.root_solved, lifted.root_solved) << domain;
        EXPECT_EQ(grounded.plan_length, lifted.plan_length) << domain;
        EXPECT_EQ(grounded.statistics.num_rollouts, lifted.statistics.num_rollouts) << domain;
        EXPECT_EQ(grounded.statistics.num_generated_states, lifted.statistics.num_generated_states) << domain;
        EXPECT_EQ(grounded.statistics.num_case_1, lifted.statistics.num_case_1) << domain;
        EXPECT_EQ(grounded.statistics.num_case_2, lifted.statistics.num_case_2) << domain;
        EXPECT_EQ(grounded.statistics.num_case_3, lifted.statistics.num_case_3) << domain;
        EXPECT_EQ(grounded.statistics.num_case_4, lifted.statistics.num_case_4) << domain;
        EXPECT_EQ(grounded.statistics.num_solved_propagations, lifted.statistics.num_solved_propagations) << domain;

        if (grounded.search_result.status == SearchStatus::SOLVED)
        {
            EXPECT_TRUE(replay_plan_steps(grounded_context, grounded.plan_steps)) << domain;
            EXPECT_TRUE(replay_plan_steps(lifted_context, lifted.plan_steps)) << domain;
        }
    }
}

/// A budget that expires must still return a well-formed, explicitly uncertified result.
TEST(MimirTests, SearchAlgorithmsRolloutIWBudgetExpiryTest)
{
    {
        const auto context = create_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
        auto options = rollout_iw::Options();
        options.max_num_states = 3;

        const auto result = rollout_iw::find_solution(context, options);

        EXPECT_EQ(result.search_result.status, SearchStatus::OUT_OF_STATES);
        EXPECT_EQ(result.stop_reason, "state budget expired");
        EXPECT_FALSE(result.root_solved);
        EXPECT_FALSE(result.search_result.plan.has_value());
        EXPECT_LE(result.statistics.num_generated_states, 3u);
    }

    {
        const auto context = create_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
        auto control = SearchControl();
        control.request_cancel();

        auto options = rollout_iw::Options();
        options.control = &control;

        const auto result = rollout_iw::find_solution(context, options);

        EXPECT_EQ(result.search_result.status, SearchStatus::CANCELED);
        EXPECT_EQ(result.stop_reason, "canceled by shared search control");
        EXPECT_FALSE(result.root_solved);
    }
}

/// A start state that is already a goal yields the empty plan rather than a rollout.
TEST(MimirTests, SearchAlgorithmsRolloutIWGoalAtStartTest)
{
    const auto context = create_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto reference = rollout_iw::find_solution(context);
    ASSERT_EQ(reference.search_result.status, SearchStatus::SOLVED);

    auto options = rollout_iw::Options();
    options.start_state = reference.search_result.goal_state;

    const auto result = rollout_iw::find_solution(context, options);

    EXPECT_EQ(result.search_result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan_length, 0u);
    EXPECT_TRUE(result.plan_steps.empty());
    EXPECT_EQ(result.statistics.num_rollouts, 0u);
}
}
