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

/// Native observation of a width search: the statistics an event handler collects without any
/// callback crossing into another language, and the search tree the tree handler captures.

#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/algorithms/brfs.hpp"
#include "mimir/search/algorithms/brfs/event_handlers.hpp"
#include "mimir/search/algorithms/iw.hpp"
#include "mimir/search/algorithms/iw/event_handlers.hpp"
#include "mimir/search/algorithms/iw/pruning_strategy.hpp"
#include "mimir/search/algorithms/strategies/goal_strategy.hpp"
#include "mimir/search/algorithms/strategies/layer_ordering_strategy.hpp"
#include "mimir/search/algorithms/strategies/pruning_strategy.hpp"
#include "mimir/search/plan.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <gtest/gtest.h>
#include <string>
#include <unordered_map>
#include <vector>

using namespace mimir::search;
using namespace mimir::formalism;

namespace mimir::tests
{
namespace
{
namespace fs = std::filesystem;

/// @brief One grounded search context.
///
/// Two contexts built from the same files do not agree on ground-action indices -- grounding breaks
/// match-tree ties on pointer order -- so any test that compares two runs event by event has to run
/// both over the *same* context. Search nodes and novelty tables are per-run state, so a second
/// search over a context that already holds states behaves like the first.
SearchContext make_grounded_context(const std::string& domain, const std::string& problem)
{
    return SearchContextImpl::create(fs::path(std::string(DATA_DIR) + domain),
                                     fs::path(std::string(DATA_DIR) + problem),
                                     SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
}

/// @brief An event handler that counts every event itself, the way a Python callback handler would.
///
/// It exists to be compared against `EventHandlerBase`'s own counters: if the two disagree, the base
/// is either missing an event or double-counting one.
class CountingEventHandlerImpl : public brfs::IEventHandler
{
public:
    uint64_t num_generated = 0;
    uint64_t num_generated_in_search_tree = 0;
    uint64_t num_generated_not_in_search_tree = 0;
    uint64_t num_expanded = 0;
    uint64_t num_expanded_goal_states = 0;
    uint64_t num_end_search_calls = 0;
    std::vector<int64_t> finished_g_values;

    /// The parent and action of every admitted transition, in admission order -- the tree handler's
    /// capture has to agree with this.
    std::vector<std::tuple<Index, Index, Index>> admitted_transitions;

    void on_expand_state(const State& state) override { ++num_expanded; }
    void on_expand_goal_state(const State& state) override { ++num_expanded_goal_states; }
    void on_generate_state(const State&, GroundAction, ContinuousCost, const State&) override { ++num_generated; }

    void on_generate_state_in_search_tree(const State& state, GroundAction action, ContinuousCost, const State& successor_state) override
    {
        ++num_generated_in_search_tree;
        admitted_transitions.emplace_back(state.get_index(), action->get_index(), successor_state.get_index());
    }

    void on_generate_state_not_in_search_tree(const State&, GroundAction, ContinuousCost, const State&) override { ++num_generated_not_in_search_tree; }

    void on_finish_g_layer(DiscreteCost g_value) override { finished_g_values.push_back(static_cast<int64_t>(g_value)); }
    void on_start_search(const State&) override {}
    void on_end_search(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) override { ++num_end_search_calls; }
    void on_solved(const Plan&) override {}
    void on_unsolvable() override {}
    void on_exhausted() override {}
    const brfs::Statistics& get_statistics() const override { return m_statistics; }

private:
    brfs::Statistics m_statistics;
};

using CountingEventHandler = std::shared_ptr<CountingEventHandlerImpl>;

/// @brief Capture the admitted tree and nothing else.
brfs::ObservationOptions tree_only_options()
{
    auto options = brfs::ObservationOptions {};
    options.capture_search_tree = true;
    return options;
}

brfs::Options make_projective_iw1_options(const Problem& problem, brfs::EventHandler event_handler)
{
    auto options = brfs::Options {};
    options.event_handler = std::move(event_handler);
    options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, true, false, false);
    return options;
}
}

TEST(MimirTests, SearchObservationBrFSNativeCountsMatchEventCountsTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    auto counting_handler = std::make_shared<CountingEventHandlerImpl>();
    auto counting_options = brfs::Options {};
    counting_options.event_handler = counting_handler;
    const auto counting_result = brfs::find_solution(context, counting_options);

    const auto native_handler = brfs::DefaultEventHandlerImpl::create(context->get_problem());
    auto native_options = brfs::Options {};
    native_options.event_handler = native_handler;
    const auto native_result = brfs::find_solution(context, native_options);

    ASSERT_EQ(counting_result.status, SearchStatus::SOLVED);
    ASSERT_EQ(native_result.status, SearchStatus::SOLVED);

    const auto& statistics = native_handler->get_statistics();
    EXPECT_EQ(statistics.get_num_generated(), counting_handler->num_generated);
    EXPECT_EQ(statistics.get_num_generated_in_search_tree(), counting_handler->num_generated_in_search_tree);
    EXPECT_EQ(statistics.get_num_generated_not_in_search_tree(), counting_handler->num_generated_not_in_search_tree);
    EXPECT_EQ(statistics.get_num_expanded(), counting_handler->num_expanded);
    EXPECT_EQ(statistics.get_num_expanded_goal_states(), counting_handler->num_expanded_goal_states);
    EXPECT_EQ(statistics.get_finished_g_values(), counting_handler->finished_g_values);

    // A search that ran a goal test on every node it popped classified every transition it generated.
    EXPECT_EQ(statistics.get_num_generated(), statistics.get_num_generated_in_search_tree() + statistics.get_num_generated_not_in_search_tree());
}

TEST(MimirTests, SearchObservationProjectiveIW1NativeCountsMatchEventCountsTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    auto counting_handler = std::make_shared<CountingEventHandlerImpl>();
    const auto counting_result = brfs::find_solution(context, make_projective_iw1_options(context->get_problem(), counting_handler));

    const auto native_handler = brfs::DefaultEventHandlerImpl::create(context->get_problem());
    const auto native_result = brfs::find_solution(context, make_projective_iw1_options(context->get_problem(), native_handler));

    EXPECT_EQ(counting_result.status, native_result.status);

    const auto& statistics = native_handler->get_statistics();
    EXPECT_EQ(statistics.get_num_generated(), counting_handler->num_generated);
    EXPECT_EQ(statistics.get_num_generated_in_search_tree(), counting_handler->num_generated_in_search_tree);
    EXPECT_EQ(statistics.get_num_generated_not_in_search_tree(), counting_handler->num_generated_not_in_search_tree);
    EXPECT_EQ(statistics.get_num_expanded(), counting_handler->num_expanded);
}

TEST(MimirTests, SearchObservationBeamNativeCountsMatchEventCountsTest)
{
    const auto make_options = [](const Problem& problem, brfs::EventHandler event_handler, uint32_t num_threads)
    {
        auto options = make_projective_iw1_options(problem, std::move(event_handler));
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
        options.beam_width = 8;
        options.beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
        options.parallel_beam_num_threads = num_threads;
        return options;
    };

    for (const auto num_threads : { uint32_t(1), uint32_t(4) })
    {
        const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
        auto counting_handler = std::make_shared<CountingEventHandlerImpl>();
        const auto counting_result = brfs::find_solution(context, make_options(context->get_problem(), counting_handler, num_threads));

        const auto native_handler = brfs::DefaultEventHandlerImpl::create(context->get_problem());
        const auto native_result = brfs::find_solution(context, make_options(context->get_problem(), native_handler, num_threads));

        EXPECT_EQ(counting_result.status, native_result.status) << "num_threads=" << num_threads;

        const auto& statistics = native_handler->get_statistics();
        EXPECT_EQ(statistics.get_num_generated(), counting_handler->num_generated) << "num_threads=" << num_threads;
        EXPECT_EQ(statistics.get_num_generated_in_search_tree(), counting_handler->num_generated_in_search_tree) << "num_threads=" << num_threads;
        EXPECT_EQ(statistics.get_num_generated_not_in_search_tree(), counting_handler->num_generated_not_in_search_tree) << "num_threads=" << num_threads;
        EXPECT_EQ(statistics.get_num_expanded(), counting_handler->num_expanded) << "num_threads=" << num_threads;
    }
}

/// The staged parallel-beam path reports rejections through the payloadless hooks, which used to
/// leave them uncounted. Whichever hook a path picks, the totals have to add up the same way.
TEST(MimirTests, SearchObservationParallelBeamPayloadlessCountersAddUpTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto native_handler = brfs::DefaultEventHandlerImpl::create(context->get_problem());

    auto options = make_projective_iw1_options(context->get_problem(), native_handler);
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(context->get_problem());
    options.beam_width = 8;
    options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
    options.parallel_beam_num_threads = 4;

    const auto result = brfs::find_solution(context, options);
    ASSERT_NE(result.status, SearchStatus::FAILED);

    const auto& statistics = native_handler->get_statistics();
    EXPECT_GT(statistics.get_num_generated(), 0u);
    EXPECT_GT(statistics.get_num_generated_not_in_search_tree(), 0u);
    EXPECT_EQ(statistics.get_num_generated(), statistics.get_num_generated_in_search_tree() + statistics.get_num_generated_not_in_search_tree());
}

TEST(MimirTests, SearchObservationPerLayerCountersAreCumulativeTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto native_handler = brfs::DefaultEventHandlerImpl::create(context->get_problem());

    auto options = brfs::Options {};
    options.event_handler = native_handler;
    options.stop_if_goal = false;  ///< exhaust, so every layer is finished
    ASSERT_EQ(brfs::find_solution(context, options).status, SearchStatus::EXHAUSTED);

    const auto& statistics = native_handler->get_statistics();
    const auto& generated = statistics.get_num_generated_until_g_value();
    const auto& admitted = statistics.get_num_generated_in_search_tree_until_g_value();
    const auto& rejected = statistics.get_num_generated_not_in_search_tree_until_g_value();
    const auto& expanded = statistics.get_num_expanded_until_g_value();
    const auto& goal_states = statistics.get_num_expanded_goal_states_until_g_value();
    const auto& g_values = statistics.get_finished_g_values();

    ASSERT_FALSE(g_values.empty());
    ASSERT_EQ(generated.size(), g_values.size());
    ASSERT_EQ(admitted.size(), g_values.size());
    ASSERT_EQ(rejected.size(), g_values.size());
    ASSERT_EQ(expanded.size(), g_values.size());
    ASSERT_EQ(goal_states.size(), g_values.size());

    for (size_t layer = 0; layer < g_values.size(); ++layer)
    {
        EXPECT_EQ(generated[layer], admitted[layer] + rejected[layer]) << "layer=" << layer;
        if (layer > 0)
        {
            EXPECT_GE(generated[layer], generated[layer - 1]) << "layer=" << layer;
            EXPECT_GE(admitted[layer], admitted[layer - 1]) << "layer=" << layer;
            EXPECT_GE(rejected[layer], rejected[layer - 1]) << "layer=" << layer;
            EXPECT_GE(expanded[layer], expanded[layer - 1]) << "layer=" << layer;
        }
    }

    /* A layer is finished when a node of the *next* layer is popped, so the deepest layer's own
       transitions are generated after the last `on_finish_g_layer` and are counted in the totals
       without ever reaching a per-layer snapshot. The snapshots are therefore a prefix of the
       totals, not equal to them. */
    EXPECT_LE(generated.back(), statistics.get_num_generated());
    EXPECT_LE(admitted.back(), statistics.get_num_generated_in_search_tree());
    EXPECT_LE(rejected.back(), statistics.get_num_generated_not_in_search_tree());
}

TEST(MimirTests, SearchObservationEndSearchRunsOnEveryTerminalStatusTest)
{
    struct Case
    {
        const char* name;
        SearchStatus expected_status;
        std::function<void(brfs::Options&)> configure;
    };

    const auto cases = std::vector<Case> {
        { "solved", SearchStatus::SOLVED, [](brfs::Options&) {} },
        { "exhausted", SearchStatus::EXHAUSTED, [](brfs::Options& options) { options.stop_if_goal = false; } },
        { "out_of_time", SearchStatus::OUT_OF_TIME, [](brfs::Options& options) { options.max_time_in_ms = 0; } },
        { "out_of_states", SearchStatus::OUT_OF_STATES, [](brfs::Options& options) { options.max_num_states = 1; } },
    };

    for (const auto& test_case : cases)
    {
        const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
        auto counting_handler = std::make_shared<CountingEventHandlerImpl>();
        auto options = brfs::Options {};
        options.event_handler = counting_handler;
        test_case.configure(options);

        const auto result = brfs::find_solution(context, options);
        EXPECT_EQ(result.status, test_case.expected_status) << test_case.name;
        EXPECT_EQ(counting_handler->num_end_search_calls, 1u) << test_case.name;
    }
}

/// A static goal that cannot hold leaves the search before it expands anything. Its statistics still
/// have to be finalized -- that is the terminal path most likely to be forgotten.
TEST(MimirTests, SearchObservationEndSearchRunsOnUnsolvableTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    auto counting_handler = std::make_shared<CountingEventHandlerImpl>();

    auto options = brfs::Options {};
    options.event_handler = counting_handler;
    options.goal_strategy = ProblemGoalStrategyImpl::create(context->get_problem());
    // Prune the initial state, which is BrFS's earliest possible exit.
    options.pruning_strategy = iw::ArityZeroNoveltyPruningStrategyImpl::create(context->get_state_repository()->get_or_create_initial_state().first);
    options.start_state = context->get_state_repository()->get_or_create_initial_state().first;

    const auto result = brfs::find_solution(context, options);
    EXPECT_EQ(counting_handler->num_end_search_calls, 1u) << "status=" << static_cast<int>(result.status);
}

TEST(MimirTests, SearchObservationIWEndSearchRunsOnEveryTerminalStatusTest)
{
    class CountingIWEventHandlerImpl : public iw::IEventHandler
    {
    public:
        uint64_t num_end_search_calls = 0;

        void on_start_search(const State&) override {}
        void on_start_arity_search(const State&, size_t) override {}
        void on_end_arity_search(const brfs::Statistics& brfs_statistics) override { m_statistics.push_back_algorithm_statistics(brfs_statistics); }
        void on_end_search() override { ++num_end_search_calls; }
        void on_solved(const Plan&) override {}
        void on_unsolvable() override {}
        void on_exhausted() override {}
        const iw::Statistics& get_statistics() const override { return m_statistics; }
        bool is_quiet() const override { return true; }

    private:
        iw::Statistics m_statistics;
    };

    // `max_arity=3` exhausts every arity on gripper without solving, which is the path that used to
    // return SearchStatus::FAILED without ever finalizing.
    for (const auto max_arity : { size_t(1), size_t(3) })
    {
        const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
        auto counting_handler = std::make_shared<CountingIWEventHandlerImpl>();

        auto options = iw::Options {};
        options.iw_event_handler = counting_handler;
        options.max_arity = max_arity;

        const auto result = iw::find_solution(context, options);
        EXPECT_EQ(counting_handler->num_end_search_calls, 1u) << "max_arity=" << max_arity << " status=" << static_cast<int>(result.status);
        EXPECT_FALSE(counting_handler->get_statistics().get_brfs_statistics_by_arity().empty()) << "max_arity=" << max_arity;
    }
}

TEST(MimirTests, SearchObservationSearchTreeMatchesAdmittedTransitionsTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    auto counting_handler = std::make_shared<CountingEventHandlerImpl>();
    const auto counting_result = brfs::find_solution(context, make_projective_iw1_options(context->get_problem(), counting_handler));

    const auto tree_handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), tree_only_options());
    const auto tree_result = brfs::find_solution(context, make_projective_iw1_options(context->get_problem(), tree_handler));

    ASSERT_EQ(counting_result.status, tree_result.status);

    const auto& tree = tree_handler->get_observation().get_search_tree();
    const auto& nodes = tree.get_nodes();

    // The root, plus one node per admitted transition, in admission order.
    ASSERT_EQ(nodes.size(), counting_handler->admitted_transitions.size() + 1);
    EXPECT_FALSE(nodes.front().parent_node.has_value());
    EXPECT_EQ(nodes.front().depth, 0u);

    for (size_t i = 0; i < counting_handler->admitted_transitions.size(); ++i)
    {
        const auto& [parent_state, action_index, successor_state] = counting_handler->admitted_transitions[i];
        const auto& node = nodes[i + 1];

        EXPECT_EQ(node.state, successor_state) << "node=" << (i + 1);
        EXPECT_EQ(node.incoming_action, action_index) << "node=" << (i + 1);
        ASSERT_TRUE(node.parent_node.has_value()) << "node=" << (i + 1);
        EXPECT_EQ(nodes[*node.parent_node].state, parent_state) << "node=" << (i + 1);
        EXPECT_EQ(node.depth, nodes[*node.parent_node].depth + 1) << "node=" << (i + 1);
    }
}

/// Following parent indices has to reproduce the same action sequence the search's own predecessor
/// relation gives, which for a solved run is the plan it returns.
TEST(MimirTests, SearchObservationSearchTreeActionPathMatchesPlanTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto tree_handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), tree_only_options());

    auto options = brfs::Options {};
    options.event_handler = tree_handler;

    const auto result = brfs::find_solution(context, options);
    ASSERT_EQ(result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(result.goal_state.has_value());

    const auto& tree = tree_handler->get_observation().get_search_tree();
    const auto goal_node = tree.find_node_by_state(result.goal_state.value().get_index());
    ASSERT_TRUE(goal_node.has_value());

    auto expected_action_indices = std::vector<Index> {};
    for (const auto action : result.plan.value().get_actions())
    {
        expected_action_indices.push_back(action->get_index());
    }

    EXPECT_EQ(tree.get_action_indices(*goal_node), expected_action_indices);
    EXPECT_EQ(tree.get_state_indices(*goal_node).size(), expected_action_indices.size() + 1);
    EXPECT_EQ(tree.get_nodes()[*goal_node].depth, expected_action_indices.size());
}

/// Capturing the tree says nothing about wanting novelty witnesses, and a pruning strategy that can
/// answer witness queries would otherwise be asked once per transition for nothing.
TEST(MimirTests, SearchObservationSearchTreeDoesNotRequestNoveltyWitnessesTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    const auto tree_handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), tree_only_options());
    EXPECT_FALSE(tree_handler->supports_novel_witness_events());

    // Nor does verbosity by itself imply witness support any more.
    const auto verbose_default_handler = brfs::DefaultEventHandlerImpl::create(context->get_problem(), false);
    EXPECT_FALSE(verbose_default_handler->supports_novel_witness_events());
}
}
