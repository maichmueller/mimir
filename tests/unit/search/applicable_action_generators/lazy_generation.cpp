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
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/applicable_action_generators.hpp"
#include "mimir/search/axiom_evaluators.hpp"
#include "mimir/search/grounders.hpp"
#include "mimir/search/state_repository.hpp"

#include <gtest/gtest.h>

using namespace mimir::search;
using namespace mimir::formalism;

namespace mimir::tests
{

namespace
{
/// @brief How many ground actions the problem has interned so far.
///
/// This is the cost that an existence test is trying not to pay: on the states this feature exists
/// for, each of these is a ground action that had to be constructed and hashed to answer a question
/// that needed one bit.
size_t num_interned_ground_actions(const Problem& problem)
{
    return boost::hana::at_key(problem->get_repositories().get_hana_repositories(), boost::hana::type<GroundActionImpl> {}).size();
}

GroundActionList collect(mimir::generator<GroundAction> action_generator)
{
    auto actions = GroundActionList {};
    for (const auto& action : action_generator)
    {
        actions.push_back(action);
    }
    return actions;
}
}

/// The lifted generator must ground exactly the actions that are consumed, and not one more.
TEST(MimirTests, SearchApplicableActionGeneratorsLiftedGroundsOnlyWhatIsConsumedTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "gripper/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl");
    const auto problem = ProblemImpl::create(domain_file, problem_file);

    const auto applicable_action_generator = KPKCLiftedApplicableActionGeneratorImpl::create(problem, SearchContextImpl::LiftedOptions::KPKCOptions());
    const auto axiom_evaluator = KPKCLiftedAxiomEvaluatorImpl::create(problem);
    const auto state_repository = StateRepositoryImpl::create(axiom_evaluator);
    const auto [state, metric_value] = state_repository->get_or_create_initial_state();

    ASSERT_EQ(num_interned_ground_actions(problem), 0);

    {
        auto action_generator = applicable_action_generator->create_applicable_action_generator(state);
        auto it = action_generator.begin();
        ASSERT_NE(it, action_generator.end());
        EXPECT_EQ(num_interned_ground_actions(problem), 1);

        ++it;
        EXPECT_EQ(num_interned_ground_actions(problem), 2);
        // The generator is abandoned here, mid-enumeration.
    }

    EXPECT_EQ(num_interned_ground_actions(problem), 2);

    const auto all_actions = collect(applicable_action_generator->create_applicable_action_generator(state));
    EXPECT_EQ(all_actions.size(), 6);
    EXPECT_EQ(num_interned_ground_actions(problem), 6);
}

/// The existence test must stop at the first action rather than enumerate the state.
TEST(MimirTests, SearchApplicableActionGeneratorsLiftedHasApplicableActionStopsAtFirstTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "gripper/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl");
    const auto problem = ProblemImpl::create(domain_file, problem_file);

    const auto applicable_action_generator = KPKCLiftedApplicableActionGeneratorImpl::create(problem, SearchContextImpl::LiftedOptions::KPKCOptions());
    const auto axiom_evaluator = KPKCLiftedAxiomEvaluatorImpl::create(problem);
    const auto state_repository = StateRepositoryImpl::create(axiom_evaluator);
    const auto [state, metric_value] = state_repository->get_or_create_initial_state();

    EXPECT_TRUE(applicable_action_generator->has_applicable_action(state));
    EXPECT_EQ(num_interned_ground_actions(problem), 1);
}

/// A state with no applicable action is reported as such, by both generators, and the lazy and
/// eager enumerations agree with the answer.
TEST(MimirTests, SearchApplicableActionGeneratorsHasApplicableActionOnDeadEndTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "deadend/domain.pddl");

    for (const auto& [problem_name, expected_num_actions] :
         std::vector<std::pair<std::string, size_t>> { { "deadend/test_problem.pddl", 0 }, { "deadend/live_problem.pddl", 1 } })
    {
        const auto problem_file = fs::path(std::string(DATA_DIR) + problem_name);

        {
            const auto problem = ProblemImpl::create(domain_file, problem_file);
            const auto applicable_action_generator = KPKCLiftedApplicableActionGeneratorImpl::create(problem, SearchContextImpl::LiftedOptions::KPKCOptions());
            const auto state_repository = StateRepositoryImpl::create(KPKCLiftedAxiomEvaluatorImpl::create(problem));
            const auto [state, metric_value] = state_repository->get_or_create_initial_state();

            EXPECT_EQ(collect(applicable_action_generator->create_applicable_action_generator(state)).size(), expected_num_actions) << problem_name;
            EXPECT_EQ(applicable_action_generator->has_applicable_action(state), expected_num_actions > 0) << problem_name;
        }

        {
            const auto problem = ProblemImpl::create(domain_file, problem_file);
            auto grounder = LiftedGrounder(problem);
            const auto applicable_action_generator = grounder.create_grounded_applicable_action_generator(match_tree::Options());
            const auto state_repository = StateRepositoryImpl::create(grounder.create_grounded_axiom_evaluator(match_tree::Options()));
            const auto [state, metric_value] = state_repository->get_or_create_initial_state();

            EXPECT_EQ(collect(applicable_action_generator->create_applicable_action_generator(state)).size(), expected_num_actions) << problem_name;
            EXPECT_EQ(applicable_action_generator->has_applicable_action(state), expected_num_actions > 0) << problem_name;
        }
    }
}

/// Making the grounded walk lazy must not reorder it: the match tree is walked in the same order,
/// one node at a time instead of all at once.
TEST(MimirTests, SearchApplicableActionGeneratorsGroundedLazyOrderMatchesEagerTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "miconic-fulladl/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "miconic-fulladl/test_problem.pddl");
    const auto problem = ProblemImpl::create(domain_file, problem_file);

    auto grounder = LiftedGrounder(problem);
    const auto applicable_action_generator = grounder.create_grounded_applicable_action_generator(match_tree::Options());
    const auto state_repository = StateRepositoryImpl::create(grounder.create_grounded_axiom_evaluator(match_tree::Options()));
    const auto [state, metric_value] = state_repository->get_or_create_initial_state();

    const auto lazy_actions = collect(applicable_action_generator->create_applicable_action_generator(state));
    ASSERT_FALSE(lazy_actions.empty());

    /* Interleaving two generators over the same match tree is the case the thread-local traversal
       stack of `generate_applicable_elements_iteratively` cannot survive, and the reason each lazy
       generator owns its stack. */
    auto outer = applicable_action_generator->create_applicable_action_generator(state);
    auto outer_it = outer.begin();
    const auto interleaved_actions = collect(applicable_action_generator->create_applicable_action_generator(state));
    ASSERT_NE(outer_it, outer.end());

    EXPECT_EQ(interleaved_actions, lazy_actions);
    EXPECT_EQ(*outer_it, lazy_actions.front());
}

}
