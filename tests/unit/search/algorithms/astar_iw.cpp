#include "mimir/search/algorithms/astar_iw.hpp"

#include "mimir/search/algorithms/astar_iw/event_handlers.hpp"
#include "mimir/search/algorithms/iw.hpp"
#include "mimir/search/algorithms/iw/novelty_table.hpp"
#include "mimir/search/applicable_action_generators.hpp"
#include "mimir/search/axiom_evaluators.hpp"
#include "mimir/search/grounders.hpp"
#include "mimir/search/heuristics.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <gtest/gtest.h>

using namespace mimir::formalism;
using namespace mimir::search;

namespace mimir::tests
{
namespace
{

struct Fixture
{
    Problem problem;
    LiftedGrounder grounder;
    GroundedAxiomEvaluator axiom_evaluator;
    StateRepository state_repository;
    GroundedApplicableActionGenerator action_generator;
    SearchContext context;
    Heuristic heuristic;

    explicit Fixture(std::string domain = "blocks_3/domain.pddl", std::string instance = "blocks_3/test_problem.pddl") :
        problem(ProblemImpl::create(fs::path(std::string(DATA_DIR) + domain), fs::path(std::string(DATA_DIR) + instance))),
        grounder(problem),
        axiom_evaluator(grounder.create_grounded_axiom_evaluator()),
        state_repository(StateRepositoryImpl::create(axiom_evaluator)),
        action_generator(grounder.create_grounded_applicable_action_generator()),
        context(SearchContextImpl::create(problem, action_generator, state_repository)),
        heuristic(BlindHeuristicImpl::create(problem))
    {
    }
};

}

TEST(MimirTests, SearchAlgorithmsAStarIWClassicalSolves)
{
    auto fixture = Fixture {};
    auto handler = astar_iw::DefaultEventHandlerImpl::create(fixture.problem);
    auto options = astar_iw::Options {};
    options.width = 2;
    options.event_handler = handler;

    const auto result = astar_iw::find_solution(fixture.context, fixture.heuristic, options);
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(result.plan.has_value());
    EXPECT_GT(result.plan->get_length(), 0);
    EXPECT_GT(handler->get_statistics().get_num_generated(), 0);
    EXPECT_GT(handler->get_statistics().get_num_expanded(), 0);
}

TEST(MimirTests, SearchAlgorithmsAStarIWAbstractedModesSolve)
{
    for (const auto mode : { astar_iw::NoveltyFeatureMode::ABSTRACTED, astar_iw::NoveltyFeatureMode::BASE_ABSTRACTED })
    {
        auto fixture = Fixture {};
        auto options = astar_iw::Options {};
        options.width = 2;
        options.novelty_feature_mode = mode;
        options.heuristic_weight = mode == astar_iw::NoveltyFeatureMode::BASE_ABSTRACTED ? 2.0 : 1.0;
        const auto result = astar_iw::find_solution(fixture.context, fixture.heuristic, options);
        EXPECT_EQ(result.status, SearchStatus::SOLVED);
    }
}

TEST(MimirTests, SearchAlgorithmsAStarIWValidatesOptions)
{
    auto fixture = Fixture {};
    auto options = astar_iw::Options {};
    options.heuristic_weight = -1;
    EXPECT_THROW(astar_iw::find_solution(fixture.context, fixture.heuristic, options), std::invalid_argument);

    options = astar_iw::Options {};
    options.novelty_feature_mode = astar_iw::NoveltyFeatureMode::ABSTRACTED;
    options.width = 4;
    EXPECT_THROW(astar_iw::find_solution(fixture.context, fixture.heuristic, options), std::invalid_argument);
}

TEST(MimirTests, SearchAlgorithmsAStarIWMatchesIWWidthBoundary)
{
    for (const auto width : { size_t(1), size_t(2) })
    {
        auto astar_fixture = Fixture("astar_iw/domain.pddl", "astar_iw/width_two_problem.pddl");
        auto astar_options = astar_iw::Options {};
        astar_options.width = width;
        auto astar_handler = astar_iw::DefaultEventHandlerImpl::create(astar_fixture.problem);
        astar_options.event_handler = astar_handler;
        const auto astar_result = astar_iw::find_solution(astar_fixture.context, astar_fixture.heuristic, astar_options);

        auto iw_fixture = Fixture("astar_iw/domain.pddl", "astar_iw/width_two_problem.pddl");
        auto iw_options = iw::Options {};
        iw_options.max_arity = width;
        const auto iw_result = iw::find_solution(iw_fixture.context, iw_options);

        EXPECT_EQ(astar_result.status == SearchStatus::SOLVED, iw_result.status == SearchStatus::SOLVED);
        EXPECT_EQ(astar_result.status == SearchStatus::SOLVED, width == 2);
        if (width == 1)
        {
            EXPECT_GT(astar_handler->get_statistics().get_num_novelty_rejected(), 0);
        }
    }
}

TEST(MimirTests, SearchAlgorithmsAStarIWRootGoalOnlyException)
{
    auto enabled_fixture = Fixture("astar_iw/domain.pddl", "astar_iw/root_negative_goal_problem.pddl");
    auto enabled_options = astar_iw::Options {};
    enabled_options.width = 1;
    enabled_options.allow_non_novel_root_goal = true;
    const auto enabled_result = astar_iw::find_solution(enabled_fixture.context, enabled_fixture.heuristic, enabled_options);
    EXPECT_EQ(enabled_result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(enabled_result.plan.has_value());
    EXPECT_EQ(enabled_result.plan->get_length(), 1);

    auto strict_fixture = Fixture("astar_iw/domain.pddl", "astar_iw/root_negative_goal_problem.pddl");
    auto strict_options = enabled_options;
    strict_options.allow_non_novel_root_goal = false;
    const auto strict_result = astar_iw::find_solution(strict_fixture.context, strict_fixture.heuristic, strict_options);
    EXPECT_EQ(strict_result.status, SearchStatus::EXHAUSTED);
}

TEST(MimirTests, SearchAlgorithmsAStarIWRejectsNonUnitCosts)
{
    auto fixture = Fixture("zenotravel/numeric/domain.pddl", "zenotravel/numeric/test_problem.pddl");
    const auto options = astar_iw::Options {};
    EXPECT_THROW(astar_iw::find_solution(fixture.context, fixture.heuristic, options), std::invalid_argument);
}

TEST(MimirTests, SearchAlgorithmsAStarIWForwardsBudgets)
{
    auto state_fixture = Fixture {};
    auto state_options = astar_iw::Options {};
    state_options.width = 2;
    state_options.max_num_states = 1;
    EXPECT_EQ(astar_iw::find_solution(state_fixture.context, state_fixture.heuristic, state_options).status, SearchStatus::OUT_OF_STATES);

    auto time_fixture = Fixture {};
    auto time_options = astar_iw::Options {};
    time_options.width = 2;
    time_options.max_time_in_ms = 0;
    EXPECT_EQ(astar_iw::find_solution(time_fixture.context, time_fixture.heuristic, time_options).status, SearchStatus::OUT_OF_TIME);
}

TEST(MimirTests, SearchAlgorithmsMinimumGNoveltyLowersTupleDepths)
{
    auto fixture = Fixture {};
    const auto [start_state, start_g] = fixture.state_repository->get_or_create_initial_state();
    auto table = iw::MinimumGNoveltyTable(2);
    EXPECT_TRUE(table.test_novelty_and_update_table(start_state, start_g));
    EXPECT_TRUE(table.test_novelty_at_g_read_only(start_state, start_g));

    auto actions = fixture.action_generator->create_applicable_action_generator(start_state);
    const auto action = *actions.begin();
    const auto [successor, successor_g] = fixture.state_repository->get_or_create_successor_state(start_state, action, start_g);
    EXPECT_TRUE(table.test_novelty_and_update_table(start_state, successor, successor_g));
    EXPECT_TRUE(table.test_novelty_at_g_read_only(successor, successor_g));
    EXPECT_FALSE(table.test_novelty_and_update_table(start_state, successor, successor_g));

    EXPECT_TRUE(table.test_novelty_and_update_table(start_state, successor, start_g));
    EXPECT_FALSE(table.test_novelty_at_g_read_only(successor, successor_g));
}

}
