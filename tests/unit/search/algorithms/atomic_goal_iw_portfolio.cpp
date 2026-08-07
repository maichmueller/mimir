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

/// Gate for T6 of docs/ROLLOUT_IW_IMPLEMENTATION_PLAN.md: the atomic-goal portfolio, in both modes.
///
/// The load-bearing invariant throughout is isolation. Nothing stops a worker from writing into the
/// parent problem's repositories except the design, and `loki::IndexedHashSet` has no locking at
/// all, so "the parent did not grow" is the assertion that stands between this feature and silent
/// memory corruption under concurrency.

#include "mimir/search/algorithms/iw/atomic_goal_portfolio.hpp"

#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/algorithms/brfs.hpp"
#include "mimir/search/algorithms/iw.hpp"
#include "mimir/search/algorithms/strategies/goal_strategy.hpp"
#include "mimir/search/applicability.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

namespace mimir::tests
{

using namespace mimir::formalism;
using namespace mimir::search;

namespace
{

/// A chain of `length + 1` locations whose only plan moves along it once. Optimal length is exactly
/// `length`, and the instance is width 1, which is what makes the certificate checkable by hand.
std::string chain_problem(size_t length)
{
    auto objects = std::string();
    auto init = std::string("(at l0)");
    for (size_t i = 0; i <= length; ++i)
    {
        objects += " l" + std::to_string(i);
        if (i < length)
        {
            init += " (succ l" + std::to_string(i) + " l" + std::to_string(i + 1) + ")";
        }
    }

    return "(define (problem chain-p) (:domain rolloutiw-chain) (:objects" + objects + " - loc) (:init " + init + ") (:goal (and (at l"
           + std::to_string(length) + "))))";
}

constexpr const char* kChainDomain = R"(
(define (domain rolloutiw-chain)
 (:requirements :strips :typing)
 (:types loc)
 (:predicates (at ?x - loc) (succ ?x ?y - loc))
 (:action move :parameters (?from ?to - loc) :precondition (and (at ?from) (succ ?from ?to)) :effect (and (not (at ?from)) (at ?to)))
)
)";

/// A chain whose goal sits off the chain, so the search exhausts without ever finding a plan --
/// which means the portfolio never reaches its finalization step, and the parent must come back
/// exactly as it went in.
std::string unreachable_chain_problem(size_t length)
{
    auto objects = std::string();
    auto init = std::string("(at l0)");
    for (size_t i = 0; i <= length; ++i)
    {
        objects += " l" + std::to_string(i);
        if (i < length)
        {
            init += " (succ l" + std::to_string(i) + " l" + std::to_string(i + 1) + ")";
        }
    }
    objects += " unreachable";

    return "(define (problem chain-unreachable) (:domain rolloutiw-chain) (:objects" + objects + " - loc) (:init " + init
           + ") (:goal (and (at unreachable))))";
}

Problem parse_chain(size_t length, bool reachable = true)
{
    return ProblemImpl::create(kChainDomain,
                               fs::path("chain-domain.pddl"),
                               reachable ? chain_problem(length) : unreachable_chain_problem(length),
                               fs::path("chain-problem.pddl"));
}

SearchContext grounded_context(const Problem& problem)
{
    return SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
}

SearchContext grounded_context(const std::string& domain, const std::string& problem)
{
    return SearchContextImpl::create(fs::path(std::string(DATA_DIR) + domain),
                                     fs::path(std::string(DATA_DIR) + problem),
                                     SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
}

SearchContext lifted_context(const Problem& problem)
{
    return SearchContextImpl::create(
        problem,
        SearchContextImpl::Options(SearchContextImpl::LiftedOptions(SearchContextImpl::LiftedOptions::KPKCOptions(SearchContextImpl::SymmetryPruning::OFF))));
}

SearchContext lifted_context(const std::string& domain, const std::string& problem)
{
    return SearchContextImpl::create(
        fs::path(std::string(DATA_DIR) + domain),
        fs::path(std::string(DATA_DIR) + problem),
        SearchContextImpl::Options(SearchContextImpl::LiftedOptions(SearchContextImpl::LiftedOptions::KPKCOptions(SearchContextImpl::SymmetryPruning::OFF))));
}

/// Per-repository element counts, in the fixed hana order.
std::vector<size_t> repository_sizes(const ProblemImpl& problem)
{
    auto sizes = std::vector<size_t> {};
    boost::hana::for_each(problem.get_repositories().get_hana_repositories(), [&](auto&& pair) { sizes.push_back(boost::hana::second(pair).size()); });
    return sizes;
}

size_t ground_action_count(const ProblemImpl& problem)
{
    return problem.get_repositories().get_hana_repositories()[boost::hana::type<GroundActionImpl> {}].size();
}

iw::AtomicGoalPortfolioOptions portfolio_options(uint32_t num_rollout_workers, uint32_t num_threads)
{
    auto options = iw::AtomicGoalPortfolioOptions();
    options.num_rollout_workers = num_rollout_workers;
    options.num_threads = num_threads;
    options.base_seed = 4711;
    return options;
}

/// One positive fluent atom out of the problem's conjunctive goal, which is what a manager hands
/// this API in practice.
///
/// Note what this does and does not buy. An atomic goal is *necessary* for the certifier to be in
/// its complete-and-optimal regime but not sufficient: IW(1) is complete and optimal for goals of
/// width at most 1, and a single atom can easily have width 2. Gripper's `(at ball1 roomb)` is one
/// -- carrying a ball into the other room makes no atom true that picking it up and moving there
/// separately did not, so IW(1) prunes the only path to it. The tests below therefore assert
/// certification where the width really is 1 (the chain instances) and only assert soundness --
/// "if it says certified, the length is genuinely optimal" -- everywhere else.
GroundAtom<FluentTag> first_goal_atom(const Problem& problem)
{
    const auto& goal_atoms = problem->get_goal_atoms<PositiveTag, FluentTag>();
    EXPECT_FALSE(goal_atoms.empty());
    return goal_atoms.front();
}

iw::AtomicGoalPortfolioOptions atomic_goal_options(const Problem& problem, uint32_t num_rollout_workers, uint32_t num_threads)
{
    auto options = portfolio_options(num_rollout_workers, num_threads);
    options.atomic_goal_atoms = GroundAtomList<FluentTag> { first_goal_atom(problem) };
    return options;
}

/// Optimal length for reaching a single atom, via an ordinary unpruned breadth-first search.
size_t optimal_length_for_atom(const SearchContext& context, GroundAtom<FluentTag> atom)
{
    auto literals = GroundLiteralLists<StaticTag, FluentTag, DerivedTag> {};
    boost::hana::at_key(literals, boost::hana::type<FluentTag> {}).push_back(context->get_problem()->get_or_create_ground_literal<FluentTag>(true, atom));
    const auto condition = context->get_problem()->get_or_create_ground_conjunctive_condition(std::move(literals), GroundNumericConstraintList {});

    auto options = brfs::Options();
    options.goal_strategy = ProblemGoalStrategyImpl::create(context->get_problem(), condition);

    const auto result = brfs::find_solution(context, options);
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    return result.plan ? result.plan->get_actions().size() : 0;
}

/// Re-apply a returned `Plan` from the initial state and confirm it reaches the goal that was
/// actually searched for -- which is the *atomic* goal, not the problem's conjunctive one, whenever
/// the caller asked for one.
bool plan_is_valid(const SearchContext& context, const Plan& plan, std::optional<GroundAtom<FluentTag>> goal_atom = std::nullopt)
{
    auto& state_repository = *context->get_state_repository();

    auto goal_condition = std::optional<GroundConjunctiveCondition> {};
    if (goal_atom)
    {
        auto literals = GroundLiteralLists<StaticTag, FluentTag, DerivedTag> {};
        boost::hana::at_key(literals, boost::hana::type<FluentTag> {})
            .push_back(context->get_problem()->get_or_create_ground_literal<FluentTag>(true, goal_atom.value()));
        goal_condition = context->get_problem()->get_or_create_ground_conjunctive_condition(std::move(literals), GroundNumericConstraintList {});
    }
    auto goal_strategy = ProblemGoalStrategyImpl::create(context->get_problem(), goal_condition);

    auto [state, metric_value] = state_repository.get_or_create_initial_state();
    for (const auto& action : plan.get_actions())
    {
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

/* ==============================================================================================
   C. Grounded portfolio
   ============================================================================================== */

/// Fixed seeds must give a reproducible answer -- as far as anything downstream of the
/// applicable-action generator can be.
///
/// Mimir's *grounded* generator does not enumerate a state's applicable actions in a reproducible
/// order: two `Problem` instances built from the same files in the same process yield different
/// orders, because the tables underneath probe by heap address. That predates this work (verified
/// against an untouched build of the baseline commit) and it feeds straight into every search whose
/// traversal depends on which action it tries first. So reproducibility is asserted where the action
/// order cannot matter -- the chain instances have exactly one applicable action per state -- and
/// everywhere else the assertion is soundness: whatever comes back must be valid, and anything
/// claimed certified must really be optimal.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioGroundedDeterministicTest)
{
    {
        const auto run = [](uint32_t workers, uint32_t threads)
        {
            const auto context = grounded_context(parse_chain(4));
            return iw::find_solution_atomic_goal_portfolio(context, portfolio_options(workers, threads));
        };

        const auto serial_first = run(4, 1);
        const auto serial_second = run(4, 1);
        const auto parallel = run(4, 4);

        for (const auto* result : { &serial_second, &parallel })
        {
            EXPECT_EQ(serial_first.status, result->status);
            EXPECT_EQ(serial_first.plan_length, result->plan_length);
            EXPECT_EQ(serial_first.certified_optimal, result->certified_optimal);
            EXPECT_EQ(serial_first.iw_lower_bound, result->iw_lower_bound);
        }

        /* *Which* worker published the winning plan is only reproducible when the workers run in a
           fixed order. Across a pool they all find the same plan and whoever gets there first is
           credited with it, so only the two serial runs are compared here. */
        EXPECT_EQ(serial_first.winning_worker, serial_second.winning_worker);
    }

    {
        const auto run = [](uint32_t workers, uint32_t threads)
        {
            const auto context = grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
            return iw::find_solution_atomic_goal_portfolio(context, atomic_goal_options(context->get_problem(), workers, threads));
        };

        const auto serial = run(4, 1);
        const auto parallel = run(4, 4);
        const auto certifier_only = run(0, 1);

        EXPECT_EQ(parallel.executed_mode, AtomicGoalPortfolioSearchMode::GROUNDED);

        /* K = 0 is the certifier on its own. Adding accelerators may find a plan the certifier's
           pruned space does not contain, but it must never make the answer worse. */
        if (certifier_only.status == SearchStatus::SOLVED)
        {
            EXPECT_EQ(certifier_only.winning_worker, 0u);
            EXPECT_TRUE(certifier_only.certified_optimal);
            EXPECT_EQ(serial.status, SearchStatus::SOLVED);
            EXPECT_LE(serial.plan_length, certifier_only.plan_length);
            EXPECT_LE(parallel.plan_length, certifier_only.plan_length);
        }

        /* Whatever any of them claims to have certified really is optimal, cross-checked against an
           unpruned breadth-first search for the same atom. This is the assertion that would catch
           an unsound certificate, and it holds regardless of the goal's width. */
        for (const auto* result : { &serial, &parallel, &certifier_only })
        {
            if (result->certified_optimal)
            {
                const auto reference_context = grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
                EXPECT_EQ(result->plan_length, optimal_length_for_atom(reference_context, first_goal_atom(reference_context->get_problem())));
            }
            if (result->status == SearchStatus::SOLVED)
            {
                ASSERT_TRUE(result->plan.has_value());
            }
        }
    }
}

/// The chain instances are genuinely width 1, so the certifier is complete and optimal there and
/// every strong claim can be asserted unconditionally -- in both modes.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioWidthOneGoalIsCertifiedTest)
{
    for (size_t length = 1; length <= 4; ++length)
    {
        for (const auto lifted : { false, true })
        {
            const auto problem = parse_chain(length);
            const auto context = lifted ? lifted_context(problem) : grounded_context(problem);

            const auto result = iw::find_solution_atomic_goal_portfolio(context, portfolio_options(3, 4));

            ASSERT_EQ(result.status, SearchStatus::SOLVED) << length << (lifted ? " lifted" : " grounded");
            EXPECT_EQ(result.plan_length, length) << length << (lifted ? " lifted" : " grounded");
            EXPECT_TRUE(result.certified_optimal) << length << (lifted ? " lifted" : " grounded");
            EXPECT_EQ(result.stop_reason, "certified shortest under width-1 assumptions");
            ASSERT_TRUE(result.plan.has_value());
            EXPECT_TRUE(plan_is_valid(context, result.plan.value()));
        }
    }
}

/// A conjunctive goal is a different regime and must be reported as such. Gripper is not width 1,
/// so the certifier finds nothing at all while the rollout workers do -- and a plan the certifier's
/// space provably does not contain must never come back marked certified.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioConjunctiveGoalIsUncertifiedTest)
{
    const auto context = grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto result = iw::find_solution_atomic_goal_portfolio(context, portfolio_options(4, 4));

    if ((result.status == SearchStatus::SOLVED) && (result.winning_worker != 0) && (result.certifier_status != SearchStatus::SOLVED)
        && ((result.certifier_status == SearchStatus::EXHAUSTED) || (result.certifier_status == SearchStatus::FAILED)))
    {
        EXPECT_FALSE(result.certified_optimal);
        EXPECT_EQ(result.stop_reason,
                  "best plan found by a rollout worker; the certifier found no plan in the width-1 space at all, so its bound does not apply");
        ASSERT_TRUE(result.plan.has_value());
        EXPECT_TRUE(plan_is_valid(context, result.plan.value()));
    }
}

/// The node-pop certificate, checked at plan lengths 1, 2 and 3.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioCertificateTest)
{
    for (size_t length = 1; length <= 3; ++length)
    {
        const auto context = grounded_context(parse_chain(length));
        const auto result = iw::find_solution_atomic_goal_portfolio(context, portfolio_options(3, 4));

        ASSERT_EQ(result.status, SearchStatus::SOLVED) << "length " << length;
        EXPECT_EQ(result.plan_length, length) << "length " << length;
        EXPECT_TRUE(result.certified_optimal) << "length " << length;

        /* Finishing every layer up to `length - 1` without popping a goal is exactly the proof that
           nothing shorter exists, so the lower bound must have caught up with the plan. */
        EXPECT_EQ(result.iw_lower_bound, length) << "length " << length;
        EXPECT_EQ(result.iw_completed_depth, length - 1) << "length " << length;

        ASSERT_TRUE(result.plan.has_value());
        EXPECT_TRUE(plan_is_valid(context, result.plan.value()));
        EXPECT_EQ(result.plan->get_actions().size(), length);
        EXPECT_EQ(result.plan_steps.size(), length);
    }
}

/// A run that finds nothing never reaches finalization, so the parent must be untouched -- byte for
/// byte, across every repository and both interning tables. This is the direct test that K+1
/// grounded workers do not write into shared state while running.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioGroundedLeavesParentFrozenTest)
{
    const auto problem = parse_chain(4, /*reachable=*/false);
    const auto context = grounded_context(problem);

    const auto sizes_before = repository_sizes(*problem);
    const auto index_tree_before = problem->get_index_tree_table().size();
    const auto double_leaf_before = problem->get_double_leaf_table().size();

    const auto result = iw::find_solution_atomic_goal_portfolio(context, portfolio_options(7, 8));

    EXPECT_NE(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(repository_sizes(*problem), sizes_before);
    EXPECT_EQ(problem->get_index_tree_table().size(), index_tree_before);
    EXPECT_EQ(problem->get_double_leaf_table().size(), double_leaf_before);
}

/// When a plan *is* found, the parent grows only during finalization, and only by the plan's own
/// steps -- never by anything a worker explored.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioGroundedGrowthIsBoundedTest)
{
    const auto problem = parse_chain(4);
    const auto context = grounded_context(problem);

    const auto ground_actions_before = ground_action_count(*problem);

    const auto result = iw::find_solution_atomic_goal_portfolio(context, portfolio_options(7, 8));

    ASSERT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_LE(ground_action_count(*problem) - ground_actions_before, result.plan_length);
}

/// Cancellation has to reach every worker, not just the one that triggered it.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioCancellationIsPromptTest)
{
    const auto context = grounded_context("spanner/domain.pddl", "spanner/p15-easy.pddl");

    auto options = portfolio_options(7, 8);
    options.max_time_in_ms = 50;

    const auto start = std::chrono::steady_clock::now();
    const auto result = iw::find_solution_atomic_goal_portfolio(context, options);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

    /* Generous by two orders of magnitude: this catches "nobody is checking the flag", not jitter. */
    EXPECT_LT(elapsed.count(), 5000) << "portfolio took " << elapsed.count() << " ms to honour a 50 ms budget";

    if (result.status != SearchStatus::SOLVED)
    {
        EXPECT_FALSE(result.certified_optimal) << "a run stopped by a budget proves nothing";
    }
}

/// The published incumbent is monotone, so the portfolio can never return a plan worse than what
/// any of its members found on its own.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioIncumbentIsMonotoneTest)
{
    const auto make = [] { return grounded_context("blocks_4/domain.pddl", "blocks_4/test_problem.pddl"); };

    const auto context = make();
    const auto portfolio = iw::find_solution_atomic_goal_portfolio(context, atomic_goal_options(context->get_problem(), 4, 4));
    ASSERT_EQ(portfolio.status, SearchStatus::SOLVED);

    /* Run each member's guidance alone and check the portfolio did at least as well as the best of
       them. If publication ever let a longer plan overwrite a shorter one, this is what breaks. */
    for (uint32_t k = 0; k < 4; ++k)
    {
        const auto single_context = make();
        auto options = atomic_goal_options(single_context->get_problem(), 1, 1);
        options.base_seed = 4711 + k;

        const auto single = iw::find_solution_atomic_goal_portfolio(single_context, options);
        if (single.status == SearchStatus::SOLVED)
        {
            EXPECT_LE(portfolio.plan_length, single.plan_length) << "worker configuration " << k;
        }
    }
}

/// A goal built from atoms rather than from the problem's own goal condition: the whole point of an
/// *atomic*-goal portfolio.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioCustomAtomicGoalTest)
{
    const auto context = grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto problem = context->get_problem();

    /* Reaching an atom that already holds initially needs no actions at all. */
    ASSERT_FALSE(problem->get_fluent_initial_atoms().empty());
    auto options = portfolio_options(2, 2);
    options.atomic_goal_atoms = GroundAtomList<FluentTag> { problem->get_fluent_initial_atoms().front() };

    const auto result = iw::find_solution_atomic_goal_portfolio(context, options);

    ASSERT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan_length, 0u);
    EXPECT_TRUE(result.certified_optimal);
}

/// Misconfiguration must fail on the calling thread as an exception, never inside a worker.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioValidationTest)
{
    const auto grounded = grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto lifted = lifted_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    auto lifted_mode = portfolio_options(2, 2);
    lifted_mode.search_mode = AtomicGoalPortfolioSearchMode::LIFTED_KPKC;
    EXPECT_THROW((void) iw::find_solution_atomic_goal_portfolio(grounded, lifted_mode), std::runtime_error);

    auto grounded_mode = portfolio_options(2, 2);
    grounded_mode.search_mode = AtomicGoalPortfolioSearchMode::GROUNDED;
    EXPECT_THROW((void) iw::find_solution_atomic_goal_portfolio(lifted, grounded_mode), std::runtime_error);

    /* A start state from a different problem carries indices that mean something else here. */
    const auto other = grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    auto foreign_start = portfolio_options(1, 1);
    foreign_start.start_state = other->get_state_repository()->get_or_create_initial_state().first;
    EXPECT_THROW((void) iw::find_solution_atomic_goal_portfolio(grounded, foreign_start), std::runtime_error);
}

/* ==============================================================================================
   D. Lifted portfolio
   ============================================================================================== */

/// The one that matters: K+1 lifted workers, each grounding on demand into its own overlay, and the
/// shared parent untouched. Run on a problem with no solution so the run ends before finalization.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioLiftedLeavesParentFrozenTest)
{
    const auto problem = parse_chain(4, /*reachable=*/false);
    const auto context = lifted_context(problem);

    const auto sizes_before = repository_sizes(*problem);
    const auto index_tree_before = problem->get_index_tree_table().size();
    const auto double_leaf_before = problem->get_double_leaf_table().size();

    const auto result = iw::find_solution_atomic_goal_portfolio(context, portfolio_options(7, 8));

    EXPECT_EQ(result.executed_mode, AtomicGoalPortfolioSearchMode::LIFTED_KPKC);
    EXPECT_NE(result.status, SearchStatus::SOLVED);

    /* Every worker ground actions and axioms on demand for the whole run. None of it landed here. */
    EXPECT_EQ(repository_sizes(*problem), sizes_before);
    EXPECT_EQ(problem->get_index_tree_table().size(), index_tree_before);
    EXPECT_EQ(problem->get_double_leaf_table().size(), double_leaf_before);
}

/// Same for a run that does find a plan: growth only at finalization, bounded by the plan length.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioLiftedGrowthIsBoundedTest)
{
    const auto problem = parse_chain(4);
    const auto context = lifted_context(problem);

    const auto ground_actions_before = ground_action_count(*problem);

    const auto result = iw::find_solution_atomic_goal_portfolio(context, portfolio_options(7, 8));

    ASSERT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan_length, 4u);
    EXPECT_LE(ground_action_count(*problem) - ground_actions_before, result.plan_length);
    ASSERT_TRUE(result.plan.has_value());
    EXPECT_TRUE(plan_is_valid(context, result.plan.value()));
}

/// Grounded and lifted must agree on the answer. They search the same transition system; only where
/// the grounding lives differs.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioGroundedAndLiftedAgreeTest)
{
    const auto instances = std::vector<std::pair<std::string, std::string>> { { "gripper/domain.pddl", "gripper/test_problem.pddl" },
                                                                             { "blocks_4/domain.pddl", "blocks_4/test_problem.pddl" },
                                                                             { "spanner/domain.pddl", "spanner/test_problem.pddl" },
                                                                             { "delivery/domain.pddl", "delivery/test_problem.pddl" } };

    for (const auto& [domain, problem] : instances)
    {
        const auto grounded_ctx = grounded_context(domain, problem);
        const auto lifted_ctx = lifted_context(domain, problem);

        const auto grounded = iw::find_solution_atomic_goal_portfolio(grounded_ctx, atomic_goal_options(grounded_ctx->get_problem(), 3, 4));
        const auto lifted = iw::find_solution_atomic_goal_portfolio(lifted_ctx, atomic_goal_options(lifted_ctx->get_problem(), 3, 4));

        /* Note what is deliberately *not* asserted here: that the two modes agree on
           `certifier_status`, or on which worker won. A rollout worker that finds and certifies a
           plan cancels the certifier mid-search, so its status legitimately depends on who got there
           first -- that early stop is the point of the design. Exact cross-mode agreement is pinned
           down where it is well defined instead, on the width-1 chain instances.

           Any certified length is a claim about the real optimum and must survive being checked
           against an unpruned search -- in either mode. */
        for (const auto* result : { &grounded, &lifted })
        {
            if (result->certified_optimal)
            {
                const auto reference_context = grounded_context(domain, problem);
                EXPECT_EQ(result->plan_length, optimal_length_for_atom(reference_context, first_goal_atom(reference_context->get_problem()))) << domain;
            }
        }

        if (grounded.certified_optimal && lifted.certified_optimal)
        {
            EXPECT_EQ(grounded.plan_length, lifted.plan_length) << domain;
        }

        if (grounded.status == SearchStatus::SOLVED)
        {
            ASSERT_TRUE(grounded.plan.has_value());
            EXPECT_TRUE(plan_is_valid(grounded_ctx, grounded.plan.value(), first_goal_atom(grounded_ctx->get_problem()))) << domain;
        }
        if (lifted.status == SearchStatus::SOLVED)
        {
            ASSERT_TRUE(lifted.plan.has_value());
            EXPECT_TRUE(plan_is_valid(lifted_ctx, lifted.plan.value(), first_goal_atom(lifted_ctx->get_problem()))) << domain;
            EXPECT_EQ(lifted.plan->get_actions().size(), lifted.plan_length) << domain;
        }
    }
}

/// Axioms and derived predicates go through a *worker-local* axiom evaluator in lifted mode, which
/// grounds axiom instances into the worker's own overlay on every state.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioLiftedWithAxiomsTest)
{
    const auto context = lifted_context("philosophers/domain.pddl", "philosophers/test_problem.pddl");
    const auto problem = context->get_problem();
    ASSERT_FALSE(problem->get_problem_and_domain_axioms().empty()) << "this test is pointless without axioms";

    const auto sizes_before = repository_sizes(*problem);

    auto options = portfolio_options(3, 4);
    options.max_time_in_ms = 20000;

    const auto result = iw::find_solution_atomic_goal_portfolio(context, options);

    if (result.status == SearchStatus::SOLVED)
    {
        ASSERT_TRUE(result.plan.has_value());
        EXPECT_TRUE(plan_is_valid(context, result.plan.value()));

        const auto grounded = iw::find_solution_atomic_goal_portfolio(grounded_context("philosophers/domain.pddl", "philosophers/test_problem.pddl"),
                                                                      portfolio_options(3, 4));
        if (grounded.status == SearchStatus::SOLVED)
        {
            EXPECT_EQ(result.plan_length, grounded.plan_length);
        }
    }
    else
    {
        /* Whatever the outcome, the axiom grounding must not have reached the shared parent. */
        EXPECT_EQ(repository_sizes(*problem), sizes_before);
    }
}

/// Numeric and conditional-effect inputs must either work or fail with a clear message -- never
/// crash, and never silently return a plan that does not replay.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioNumericAndConditionalEffectsTest)
{
    const auto instances = std::vector<std::pair<std::string, std::string>> { { "fo-counters/domain.pddl", "fo-counters/test_problem.pddl" },
                                                                             { "miconic-simpleadl/domain.pddl", "miconic-simpleadl/test_problem.pddl" } };

    for (const auto& [domain, problem] : instances)
    {
        for (const auto lifted : { false, true })
        {
            const auto context = lifted ? lifted_context(domain, problem) : grounded_context(domain, problem);

            auto options = portfolio_options(2, 2);
            options.max_time_in_ms = 20000;

            auto result = iw::AtomicGoalPortfolioResult {};
            ASSERT_NO_THROW(result = iw::find_solution_atomic_goal_portfolio(context, options)) << domain << (lifted ? " lifted" : " grounded");

            if (result.status == SearchStatus::SOLVED)
            {
                ASSERT_TRUE(result.plan.has_value());
                EXPECT_TRUE(plan_is_valid(context, result.plan.value())) << domain << (lifted ? " lifted" : " grounded");
            }
        }
    }
}

/// Running everything on the calling thread must reach the same answer as running it across a pool.
///
/// Asserted on the chain instances, where the traversal does not depend on the generator's action
/// order (see the determinism test above for why that matters). On a domain with real branching the
/// two configurations can legitimately return different plans of the same quality class, so there
/// only validity and the soundness of any certificate are checked.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioSerialMatchesParallelTest)
{
    for (const auto lifted : { false, true })
    {
        const auto label = lifted ? "lifted" : "grounded";

        {
            const auto make = [&]
            {
                const auto problem = parse_chain(4);
                return lifted ? lifted_context(problem) : grounded_context(problem);
            };

            const auto serial = iw::find_solution_atomic_goal_portfolio(make(), portfolio_options(3, 1));
            const auto parallel = iw::find_solution_atomic_goal_portfolio(make(), portfolio_options(3, 4));

            ASSERT_EQ(serial.status, parallel.status) << label;
            EXPECT_EQ(serial.plan_length, parallel.plan_length) << label;
            EXPECT_EQ(serial.certified_optimal, parallel.certified_optimal) << label;
            EXPECT_EQ(serial.iw_lower_bound, parallel.iw_lower_bound) << label;
        }

        {
            const auto make = [&]
            { return lifted ? lifted_context("blocks_4/domain.pddl", "blocks_4/test_problem.pddl") :
                              grounded_context("blocks_4/domain.pddl", "blocks_4/test_problem.pddl"); };

            const auto serial_context = make();
            const auto parallel_context = make();
            const auto serial =
                iw::find_solution_atomic_goal_portfolio(serial_context, atomic_goal_options(serial_context->get_problem(), 3, 1));
            const auto parallel =
                iw::find_solution_atomic_goal_portfolio(parallel_context, atomic_goal_options(parallel_context->get_problem(), 3, 4));

            for (const auto& [result, context] : { std::pair { &serial, &serial_context }, std::pair { &parallel, &parallel_context } })
            {
                if (result->status == SearchStatus::SOLVED)
                {
                    ASSERT_TRUE(result->plan.has_value()) << label;
                    EXPECT_TRUE(plan_is_valid(*context, result->plan.value(), first_goal_atom((*context)->get_problem()))) << label;
                }
                if (result->certified_optimal)
                {
                    const auto reference_context = grounded_context("blocks_4/domain.pddl", "blocks_4/test_problem.pddl");
                    EXPECT_EQ(result->plan_length, optimal_length_for_atom(reference_context, first_goal_atom(reference_context->get_problem()))) << label;
                }
            }
        }
    }
}

/// The shared expansion budget must stop a worker that is *inside* a search, not merely between
/// searches.
///
/// Serially there is nobody else to notice: the coordinator hands control to a worker and does not
/// get it back until that worker has finished its entire run. A budget policed only by the
/// coordinator is therefore not a budget at all in serial mode -- it caps the number of workers
/// started, and a single one of them can overshoot by however long it happens to run. The cap lives
/// in `SearchControl::add_expansions` so each participant stops itself.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioExpansionBudgetStopsWorkersMidSearchTest)
{
    constexpr auto kChainLength = size_t { 40 };
    constexpr auto kBudget = uint64_t { 5 };

    // Long enough that an unpoliced certifier expands far past the budget on its own.
    const auto unbudgeted_context = grounded_context(parse_chain(kChainLength));
    auto unbudgeted_options = iw::AtomicGoalPortfolioOptions();
    unbudgeted_options.num_rollout_workers = 0;
    unbudgeted_options.num_threads = 1;
    const auto unbudgeted = iw::find_solution_atomic_goal_portfolio(unbudgeted_context, unbudgeted_options);
    ASSERT_GT(unbudgeted.total_expansions, kBudget * 3) << "instance is too small for this test to mean anything";

    const auto context = grounded_context(parse_chain(kChainLength));
    auto options = iw::AtomicGoalPortfolioOptions();
    options.num_rollout_workers = 2;
    options.num_threads = 1;  ///< serial: the coordinator cannot interrupt anyone
    options.max_total_expansions = kBudget;

    const auto result = iw::find_solution_atomic_goal_portfolio(context, options);

    /* One expansion of slack: the cap is noticed by the expansion that crosses it. */
    EXPECT_LE(result.total_expansions, kBudget + 1);
    EXPECT_FALSE(result.certified_optimal) << "a run cut short by its budget certifies nothing";
}

/// A rollout worker must not restart forever on a plan it cannot improve.
///
/// A goal one step from the start escapes the incumbent bound entirely (see
/// `SearchAlgorithmsRolloutIWDepthOneGoalEscapesIncumbentBoundTest`), so a worker that publishes a
/// length-1 incumbent gets the identical plan back from every restart: it publishes nothing new,
/// certifies nothing, and has no reason of its own to stop. In the shipped configuration the
/// certifier finds that same one-step plan and cancels everyone, which bounds the damage -- but
/// only as long as the certifier lives, so this is a liveness guard rather than a proof.
TEST(MimirTests, SearchAlgorithmsAtomicGoalPortfolioRestartLoopTerminatesTest)
{
    const auto context = grounded_context(parse_chain(1));

    auto options = iw::AtomicGoalPortfolioOptions();
    options.num_rollout_workers = 4;
    options.num_threads = 4;
    // Deliberately no time, expansion or depth budget: the loop has to terminate on its own logic.

    const auto started_at = std::chrono::steady_clock::now();
    const auto result = iw::find_solution_atomic_goal_portfolio(context, options);
    const auto elapsed = std::chrono::steady_clock::now() - started_at;

    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan_length, 1u);
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 10);

    /* Each worker either never ran, or ran once and found the one-step plan. A worker that restarts
       on a bound that did not move shows up here as an unbounded rollout count. */
    for (size_t k = 0; k < result.rollout_statistics.size(); ++k)
    {
        EXPECT_LE(result.rollout_statistics[k].num_rollouts, 4u) << "worker " << k << " kept restarting on an unchanged bound";
    }
}
}
