#include "mimir/search/algorithms/astar_iw.hpp"

#include "mimir/search/algorithms/astar_iw/event_handlers.hpp"
#include "mimir/search/algorithms/iw.hpp"
#include "mimir/search/algorithms/iw/novelty_table.hpp"
#include "mimir/search/applicable_action_generators.hpp"
#include "mimir/search/axiom_evaluators.hpp"
#include "mimir/search/grounders.hpp"
#include "mimir/search/heuristics.hpp"
#include "mimir/search/landmarks/fact_landmark_generator.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"
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

TEST(MimirTests, SearchAlgorithmsAStarIWLandmarkNoveltyClosesTheWidthOneGapInEveryMode)
{
    /* The landmark coordinate is a concrete landmark rank in every feature mode, so restricting
       the features composes with abstraction instead of replacing it -- and it earns its keep in
       all three: on this purpose-built width-2 instance, width-1 minimum-g pruning is too narrow
       under classical, abstracted and base-abstracted features alike, and the landmark
       restriction is enough to get through in each. Without this the coordinate could be inert in
       the abstracted modes and every other test here would still pass. */
    for (const auto mode :
         { astar_iw::NoveltyFeatureMode::CLASSICAL, astar_iw::NoveltyFeatureMode::ABSTRACTED, astar_iw::NoveltyFeatureMode::BASE_ABSTRACTED })
    {
        auto fixture = Fixture("astar_iw/domain.pddl", "astar_iw/width_two_problem.pddl");
        const auto landmark_graph = landmarks::ApproximateFactLandmarkGenerator::create(fixture.grounder);
        ASSERT_GT(landmark_graph->get_landmark_atom_indices().size(), 0u);

        const auto run = [&](bool with_landmarks)
        {
            auto options = astar_iw::Options {};
            options.width = 1;
            options.novelty_feature_mode = mode;
            options.event_handler = astar_iw::DefaultEventHandlerImpl::create(fixture.problem, true);
            if (with_landmarks)
            {
                options.landmark_novelty_graph = landmark_graph;
            }
            return astar_iw::find_solution(fixture.context, fixture.heuristic, options).status;
        };

        // Width-1 pruning leaves no reachable goal in the admitted subgraph, so the search runs
        // that subgraph out rather than reporting failure.
        EXPECT_EQ(run(false), SearchStatus::EXHAUSTED) << "mode " << static_cast<int>(mode);
        EXPECT_EQ(run(true), SearchStatus::SOLVED) << "mode " << static_cast<int>(mode);
    }
}

TEST(MimirTests, SearchAlgorithmsAStarIWLandmarkNoveltySolvesAtWidthOne)
{
    // Same width-gap story as IW: AStarIW's minimum-g pruning at width 1 is too narrow here, and
    // restricting the features to (landmark, atom) pairs is enough to get through.
    auto fixture = Fixture {};
    const auto landmarks = landmarks::ApproximateFactLandmarkGenerator::create(fixture.grounder);
    ASSERT_GT(landmarks->get_landmark_atom_indices().size(), 0u);

    auto handler = astar_iw::DefaultEventHandlerImpl::create(fixture.problem, true);
    auto options = astar_iw::Options {};
    options.width = 1;
    options.landmark_novelty_graph = landmarks;
    options.event_handler = handler;

    const auto result = astar_iw::find_solution(fixture.context, fixture.heuristic, options);
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(result.plan.has_value());
    EXPECT_GT(result.plan->get_length(), 0);
    EXPECT_GT(handler->get_statistics().get_num_expanded(), 0);
}

TEST(MimirTests, SearchAlgorithmsAStarIWLandmarkNoveltyWithoutLandmarksMatchesClassical)
{
    /* An empty landmark set leaves every state on the BOT coordinate, so the sparse
       landmark-restricted minimum-g table must reproduce the dense classical one exactly.

       Both runs share one `Fixture` on purpose. Two `Problem` instances of the same PDDL can
       enumerate grounded actions in different orders, and under novelty pruning generation order
       decides which candidate claims a contested tuple, so expansion counts only compare
       meaningfully on one grounded instance. */
    auto generator_options = landmarks::FactLandmarkGeneratorOptions {};
    generator_options.include_positive_goal_facts = false;

    auto fixture = Fixture {};
    const auto run = [&](astar_iw::NoveltyFeatureMode mode, bool with_empty_landmarks)
    {
        auto handler = astar_iw::DefaultEventHandlerImpl::create(fixture.problem, true);
        auto options = astar_iw::Options {};
        options.width = 2;
        options.novelty_feature_mode = mode;
        options.event_handler = handler;
        if (with_empty_landmarks)
        {
            options.landmark_novelty_graph = landmarks::ApproximateFactLandmarkGenerator::create(fixture.grounder, generator_options);
            EXPECT_EQ(options.landmark_novelty_graph->get_landmark_atom_indices().size(), 0u);
        }
        const auto result = astar_iw::find_solution(fixture.context, fixture.heuristic, options);
        return std::make_tuple(result.status, result.plan.has_value() ? result.plan->get_length() : 0, handler->get_statistics().get_num_expanded());
    };

    // Holds in every mode: without landmarks the coordinate is constant at BOT, so the composed
    // tables must be indistinguishable from the uncomposed ones.
    for (const auto mode :
         { astar_iw::NoveltyFeatureMode::CLASSICAL, astar_iw::NoveltyFeatureMode::ABSTRACTED, astar_iw::NoveltyFeatureMode::BASE_ABSTRACTED })
    {
        EXPECT_EQ(run(mode, true), run(mode, false)) << "mode " << static_cast<int>(mode);
    }
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

TEST(MimirTests, SearchAlgorithmsMinimumGNoveltyTracksLoweringOfExistingLabels)
{
    auto fixture = Fixture {};
    const auto [start_state, start_g] = fixture.state_repository->get_or_create_initial_state();
    auto table = iw::MinimumGNoveltyTable(2);

    // Labelling a tuple for the first time is not a lowering: nothing can have been stolen.
    EXPECT_TRUE(table.test_novelty_and_update_table(start_state, start_g));
    EXPECT_FALSE(table.has_lowered_existing_label());

    auto actions = fixture.action_generator->create_applicable_action_generator(start_state);
    const auto action = *actions.begin();
    const auto [successor, successor_g] = fixture.state_repository->get_or_create_successor_state(start_state, action, start_g);
    EXPECT_TRUE(table.test_novelty_and_update_table(start_state, successor, successor_g));
    EXPECT_FALSE(table.has_lowered_existing_label());

    // Re-reaching the same tuples at a strictly smaller cost is.
    EXPECT_TRUE(table.test_novelty_and_update_table(start_state, successor, start_g));
    EXPECT_TRUE(table.has_lowered_existing_label());
}

TEST(MimirTests, SearchAlgorithmsAStarIWProbeOptionDoesNotChangeTheSearch)
{
    /* `probe_novelty_before_heuristic` only moves when the heuristic is evaluated, so both
       settings must return the same plan for the same expansions.

       One `Fixture` per mode/width pair, shared by both settings, for the reason spelled out
       above: a second grounding of the same PDDL can order actions differently, and under
       novelty pruning generation order decides which candidate claims a contested tuple. */
    for (const auto mode :
         { astar_iw::NoveltyFeatureMode::CLASSICAL, astar_iw::NoveltyFeatureMode::ABSTRACTED, astar_iw::NoveltyFeatureMode::BASE_ABSTRACTED })
    {
        for (const auto width : { size_t(1), size_t(2) })
        {
            auto fixture = Fixture {};
            const auto run = [&](bool probe)
            {
                auto handler = astar_iw::DefaultEventHandlerImpl::create(fixture.problem, true);
                auto options = astar_iw::Options {};
                options.width = width;
                options.novelty_feature_mode = mode;
                options.probe_novelty_before_heuristic = probe;
                options.event_handler = handler;
                const auto result = astar_iw::find_solution(fixture.context, fixture.heuristic, options);
                return std::make_tuple(result.status,
                                       result.plan.has_value() ? result.plan->get_length() : 0,
                                       handler->get_statistics().get_num_expanded(),
                                       handler->get_statistics().get_num_generated());
            };
            EXPECT_EQ(run(true), run(false)) << "mode " << static_cast<int>(mode) << " width " << width;
        }
    }
}

TEST(MimirTests, SearchAlgorithmsMinimumGNoveltyProbeAgreesAndDoesNotWrite)
{
    auto fixture = Fixture {};
    const auto [start_state, start_g] = fixture.state_repository->get_or_create_initial_state();
    auto probed = iw::MinimumGNoveltyTable(2);
    auto committed = iw::MinimumGNoveltyTable(2);
    EXPECT_TRUE(probed.test_novelty_and_update_table(start_state, start_g));
    EXPECT_TRUE(committed.test_novelty_and_update_table(start_state, start_g));

    auto actions = fixture.action_generator->create_applicable_action_generator(start_state);
    const auto action = *actions.begin();
    const auto [successor, successor_g] = fixture.state_repository->get_or_create_successor_state(start_state, action, start_g);

    // The probe must answer what the update would have answered...
    EXPECT_TRUE(probed.test_would_improve(start_state, successor, successor_g));
    EXPECT_TRUE(committed.test_novelty_and_update_table(start_state, successor, successor_g));

    // ...and must have written nothing: the table that was only probed still reports the
    // transition as novel, and still owns no tuple at the successor's cost.
    EXPECT_FALSE(probed.test_novelty_at_g_read_only(successor, successor_g));
    EXPECT_TRUE(probed.test_would_improve(start_state, successor, successor_g));
    EXPECT_TRUE(probed.test_novelty_and_update_table(start_state, successor, successor_g));

    // Once committed on both sides, the probe agrees that there is nothing left to lower.
    EXPECT_FALSE(probed.test_would_improve(start_state, successor, successor_g));
    EXPECT_FALSE(committed.test_would_improve(start_state, successor, successor_g));
    // A strictly smaller cost still improves, and the probe sees that too.
    EXPECT_TRUE(committed.test_would_improve(start_state, successor, start_g));
}

TEST(MimirTests, SearchAlgorithmsMinimumGNoveltyWidensRanksBeyondEightBits)
{
    auto fixture = Fixture {};
    const auto [start_state, start_g] = fixture.state_repository->get_or_create_initial_state();
    auto table = iw::MinimumGNoveltyTable(2);

    // Labels are stored as ranks into the list of distinct cost values, starting at one
    // byte per tuple. Walking a strictly decreasing sequence of costs mints a new rank
    // every step and forces the storage to widen past 8 and then past 16 bits.
    constexpr auto num_costs = 70000;
    for (auto cost = num_costs; cost >= 1; --cost)
    {
        EXPECT_TRUE(table.test_novelty_and_update_table(start_state, ContinuousCost(cost)));
    }
    // Every widening must preserve the labels: the last cost written is still the label,
    // and no earlier one is.
    EXPECT_TRUE(table.test_novelty_at_g_read_only(start_state, ContinuousCost(1)));
    EXPECT_FALSE(table.test_novelty_at_g_read_only(start_state, ContinuousCost(2)));
    EXPECT_FALSE(table.test_novelty_at_g_read_only(start_state, ContinuousCost(num_costs)));
    EXPECT_FALSE(table.test_novelty_and_update_table(start_state, ContinuousCost(1)));
}

}
