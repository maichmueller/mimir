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

/// The rich half of native observation: transition logs with their disposition, novelty witnesses
/// attached to the transition that produced them, syntactic effect summaries, realized state deltas,
/// composition with a second observer, and per-arity IW observation.

#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/ground_effects.hpp"
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
#include "mimir/search/state.hpp"
#include "mimir/search/state_repository.hpp"

#include <algorithm>
#include <gtest/gtest.h>
#include <map>
#include <ranges>
#include <string>
#include <tuple>
#include <vector>

using namespace mimir::search;
using namespace mimir::formalism;

namespace mimir::tests
{
namespace
{
namespace fs = std::filesystem;

/// Two contexts built from the same files do not agree on ground-action indices, so any test that
/// compares two runs event by event runs both over one context.
SearchContext make_grounded_context(const std::string& domain, const std::string& problem)
{
    return SearchContextImpl::create(fs::path(std::string(DATA_DIR) + domain),
                                     fs::path(std::string(DATA_DIR) + problem),
                                     SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
}

brfs::Options make_projective_iw1_options(const Problem& problem, brfs::EventHandler event_handler)
{
    auto options = brfs::Options {};
    options.event_handler = std::move(event_handler);
    options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, true, false, false);
    return options;
}

brfs::ObservationOptions all_transitions_options()
{
    auto options = brfs::ObservationOptions {};
    options.capture_search_tree = true;
    options.capture_admitted_transitions = true;
    options.capture_rejected_transitions = true;
    return options;
}

/// @brief Records the raw witness events, so the observation's attribution can be checked against
/// what the search actually reported for each transition.
class WitnessRecordingEventHandlerImpl : public brfs::IEventHandler
{
public:
    std::map<std::tuple<Index, Index, Index>, iw::AtomIndexList> witnesses_by_transition;
    std::vector<std::tuple<Index, Index, Index>> admitted_transitions;
    std::vector<std::tuple<Index, Index, Index>> rejected_transitions;

    void on_expand_state(const State&) override {}
    void on_expand_goal_state(const State&) override {}
    void on_generate_state(const State&, GroundAction, ContinuousCost, const State&) override {}

    bool supports_novel_witness_events() const override { return true; }

    void on_generate_state_with_novel_witness(const State& state,
                                              GroundAction action,
                                              ContinuousCost,
                                              const State& successor_state,
                                              const iw::AtomIndexList& novel_fluent_atom_indices) override
    {
        witnesses_by_transition[std::make_tuple(state.get_index(), action->get_index(), successor_state.get_index())] = novel_fluent_atom_indices;
    }

    void on_generate_state_in_search_tree(const State& state, GroundAction action, ContinuousCost, const State& successor_state) override
    {
        admitted_transitions.emplace_back(state.get_index(), action->get_index(), successor_state.get_index());
    }

    void on_generate_state_not_in_search_tree(const State& state, GroundAction action, ContinuousCost, const State& successor_state) override
    {
        rejected_transitions.emplace_back(state.get_index(), action->get_index(), successor_state.get_index());
    }

    void on_finish_g_layer(DiscreteCost) override {}
    void on_start_search(const State&) override {}
    void on_end_search(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) override {}
    void on_solved(const Plan&) override {}
    void on_unsolvable() override {}
    void on_exhausted() override {}
    const brfs::Statistics& get_statistics() const override { return m_statistics; }

private:
    brfs::Statistics m_statistics;
};

/// @brief The add/delete counts the requirement spells out, computed straight from the action.
std::pair<uint32_t, uint32_t> count_effects_by_hand(GroundAction action)
{
    auto add_count = uint32_t(0);
    auto delete_count = uint32_t(0);
    for (const auto& conditional_effect : action->get_conditional_effects())
    {
        const auto conjunctive_effect = conditional_effect->get_conjunctive_effect();
        add_count += static_cast<uint32_t>(std::ranges::distance(conjunctive_effect->get_propositional_effects<PositiveTag>()));
        delete_count += static_cast<uint32_t>(std::ranges::distance(conjunctive_effect->get_propositional_effects<NegativeTag>()));
    }
    return { add_count, delete_count };
}
}

TEST(MimirTests, RichObservationTransitionDispositionMatchesStatisticsTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), all_transitions_options());

    const auto result = brfs::find_solution(context, make_projective_iw1_options(context->get_problem(), handler));
    ASSERT_NE(result.status, SearchStatus::FAILED);

    const auto& transitions = handler->get_observation().get_transitions();
    const auto& statistics = handler->get_statistics();

    const auto num_admitted =
        std::count_if(transitions.begin(), transitions.end(), [](const auto& t) { return t.disposition == brfs::TransitionDisposition::ADMITTED; });
    const auto num_rejected =
        std::count_if(transitions.begin(), transitions.end(), [](const auto& t) { return t.disposition == brfs::TransitionDisposition::REJECTED; });

    EXPECT_EQ(static_cast<uint64_t>(num_admitted), statistics.get_num_generated_in_search_tree());
    EXPECT_EQ(static_cast<uint64_t>(num_rejected), statistics.get_num_generated_not_in_search_tree());
    EXPECT_EQ(transitions.size(), statistics.get_num_generated());

    // Every admitted transition is the edge of a tree node, and vice versa.
    const auto& tree = handler->get_observation().get_search_tree();
    EXPECT_EQ(tree.get_num_nodes(), static_cast<size_t>(num_admitted) + 1);

    for (const auto& transition : transitions)
    {
        EXPECT_EQ(transition.successor_depth, transition.parent_depth + 1);
        if (transition.disposition == brfs::TransitionDisposition::ADMITTED)
        {
            const auto node = tree.find_node_by_state(transition.successor_state);
            ASSERT_TRUE(node.has_value());
            EXPECT_EQ(tree.get_nodes()[*node].depth, transition.successor_depth);
        }
    }
}

TEST(MimirTests, RichObservationAdmittedOnlyCaptureDropsRejectedTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    auto options = brfs::ObservationOptions {};
    options.capture_admitted_transitions = true;
    const auto handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), options);

    ASSERT_NE(brfs::find_solution(context, make_projective_iw1_options(context->get_problem(), handler)).status, SearchStatus::FAILED);

    const auto& transitions = handler->get_observation().get_transitions();
    EXPECT_GT(transitions.size(), 0u);
    EXPECT_EQ(transitions.size(), handler->get_statistics().get_num_generated_in_search_tree());
    EXPECT_TRUE(std::all_of(transitions.begin(),
                            transitions.end(),
                            [](const auto& t) { return t.disposition == brfs::TransitionDisposition::ADMITTED; }));

    // ... and the rejections were still counted, just not retained.
    EXPECT_GT(handler->get_statistics().get_num_generated_not_in_search_tree(), 0u);
}

/// Witnesses have to end up on the transition that produced them, not on whichever transition was
/// classified next -- which is a different transition entirely once a beam defers classification.
TEST(MimirTests, RichObservationWitnessesMatchTheWitnessEventsTest)
{
    for (const auto num_threads : { uint32_t(1), uint32_t(4) })
    {
        const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

        auto options = all_transitions_options();
        options.capture_novel_witnesses = true;
        const auto observation_handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), options);
        auto witness_handler = std::make_shared<WitnessRecordingEventHandlerImpl>();

        // One run, two observers: comparing two runs would compare two different searches.
        const auto composite = brfs::CompositeEventHandlerImpl::create({ observation_handler, witness_handler }, 0);

        auto search_options = make_projective_iw1_options(context->get_problem(), composite);
        if (num_threads > 1)
        {
            search_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(context->get_problem());
            search_options.beam_width = 8;
            search_options.parallel_beam_num_threads = num_threads;
        }

        ASSERT_NE(brfs::find_solution(context, search_options).status, SearchStatus::FAILED) << "num_threads=" << num_threads;

        const auto& transitions = observation_handler->get_observation().get_transitions();
        auto num_checked = size_t(0);
        for (const auto& transition : transitions)
        {
            const auto key = std::make_tuple(transition.parent_state, transition.action, transition.successor_state);
            const auto it = witness_handler->witnesses_by_transition.find(key);

            ASSERT_EQ(transition.novel_fluent_atom_indices.has_value(), it != witness_handler->witnesses_by_transition.end())
                << "num_threads=" << num_threads;
            if (transition.novel_fluent_atom_indices.has_value())
            {
                EXPECT_EQ(*transition.novel_fluent_atom_indices, it->second) << "num_threads=" << num_threads;
                ++num_checked;
            }
        }
        EXPECT_GT(num_checked, 0u) << "num_threads=" << num_threads;
    }
}

/// A pruning strategy that cannot answer witness queries and one that answers "no novel atom" are
/// different answers, and the record has to keep them apart.
TEST(MimirTests, RichObservationDistinguishesUnsupportedFromEmptyWitnessTest)
{
    auto options = all_transitions_options();
    options.capture_novel_witnesses = true;

    // Duplicate pruning supports no witness query at all.
    {
        const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
        const auto handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), options);
        auto search_options = brfs::Options {};
        search_options.event_handler = handler;
        ASSERT_EQ(brfs::find_solution(context, search_options).status, SearchStatus::SOLVED);

        const auto& transitions = handler->get_observation().get_transitions();
        ASSERT_GT(transitions.size(), 0u);
        EXPECT_TRUE(std::all_of(transitions.begin(), transitions.end(), [](const auto& t) { return !t.novel_fluent_atom_indices.has_value(); }));
    }

    // Width-1 novelty pruning answers, and some of its answers are empty.
    {
        const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
        const auto handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), options);
        ASSERT_NE(brfs::find_solution(context, make_projective_iw1_options(context->get_problem(), handler)).status, SearchStatus::FAILED);

        const auto& transitions = handler->get_observation().get_transitions();
        const auto num_present = std::count_if(transitions.begin(), transitions.end(), [](const auto& t) { return t.novel_fluent_atom_indices.has_value(); });
        const auto num_empty = std::count_if(transitions.begin(),
                                             transitions.end(),
                                             [](const auto& t) { return t.novel_fluent_atom_indices.has_value() && t.novel_fluent_atom_indices->empty(); });
        const auto num_non_empty = std::count_if(transitions.begin(),
                                                 transitions.end(),
                                                 [](const auto& t)
                                                 { return t.novel_fluent_atom_indices.has_value() && !t.novel_fluent_atom_indices->empty(); });

        EXPECT_GT(num_present, 0);
        EXPECT_GT(num_empty, 0) << "expected at least one transition whose witness query found nothing";
        EXPECT_GT(num_non_empty, 0) << "expected at least one transition with novel atoms";
    }
}

/// Witnesses are not free, so nothing but an explicit request may switch them on.
TEST(MimirTests, RichObservationDoesNotComputeWitnessesUnlessRequestedTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    EXPECT_FALSE(brfs::ObservationEventHandlerImpl::create(context->get_problem(), all_transitions_options())->supports_novel_witness_events());

    auto with_witnesses = all_transitions_options();
    with_witnesses.capture_novel_witnesses = true;
    EXPECT_TRUE(brfs::ObservationEventHandlerImpl::create(context->get_problem(), with_witnesses)->supports_novel_witness_events());
}

/// Tree and admitted-transition capture live off the payloadful admission event, which the staged
/// beam still emits, so they keep its fast path. Anything that needs the rejected side gives it up.
TEST(MimirTests, RichObservationPayloadlessSupportFollowsWhatIsCapturedTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    auto tree_only = brfs::ObservationOptions {};
    tree_only.capture_search_tree = true;
    EXPECT_TRUE(brfs::ObservationEventHandlerImpl::create(context->get_problem(), tree_only)->supports_payloadless_generated_state_events());

    auto admitted_only = brfs::ObservationOptions {};
    admitted_only.capture_admitted_transitions = true;
    EXPECT_TRUE(brfs::ObservationEventHandlerImpl::create(context->get_problem(), admitted_only)->supports_payloadless_generated_state_events());

    EXPECT_FALSE(brfs::ObservationEventHandlerImpl::create(context->get_problem(), all_transitions_options())->supports_payloadless_generated_state_events());

    auto with_witnesses = brfs::ObservationOptions {};
    with_witnesses.capture_search_tree = true;
    with_witnesses.capture_novel_witnesses = true;
    EXPECT_FALSE(brfs::ObservationEventHandlerImpl::create(context->get_problem(), with_witnesses)->supports_payloadless_generated_state_events());
}

TEST(MimirTests, RichObservationEffectSummariesMatchConditionalEffectListsTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    auto options = all_transitions_options();
    options.capture_action_effect_summaries = true;
    const auto handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), options);

    ASSERT_NE(brfs::find_solution(context, make_projective_iw1_options(context->get_problem(), handler)).status, SearchStatus::FAILED);

    const auto& observation = handler->get_observation();
    const auto& ground_action_repository =
        boost::hana::at_key(context->get_problem()->get_repositories().get_hana_repositories(), boost::hana::type<GroundActionImpl> {});

    auto distinct_actions = std::set<Index> {};
    for (const auto& transition : observation.get_transitions())
    {
        ASSERT_NE(transition.action_effect_summary, nullptr);

        const auto action = ground_action_repository.at(transition.action);
        const auto [expected_add, expected_delete] = count_effects_by_hand(action);
        EXPECT_EQ(transition.action_effect_summary->num_add_effects, expected_add);
        EXPECT_EQ(transition.action_effect_summary->num_delete_effects, expected_delete);

        // Every occurrence of one action must see the same cached summary object, not a recomputation.
        EXPECT_EQ(transition.action_effect_summary, observation.find_action_effect_summary(transition.action));
        distinct_actions.insert(transition.action);
    }

    EXPECT_GT(distinct_actions.size(), 0u);
    EXPECT_EQ(observation.get_num_action_effect_summaries(), distinct_actions.size());
}

TEST(MimirTests, RichObservationRealizedEffectsMatchStateDifferencesTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    auto options = all_transitions_options();
    options.capture_realized_effects = true;
    options.capture_action_effect_summaries = true;
    const auto handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), options);

    ASSERT_NE(brfs::find_solution(context, make_projective_iw1_options(context->get_problem(), handler)).status, SearchStatus::FAILED);

    auto& state_repository = *context->get_state_repository();
    auto num_checked = size_t(0);

    for (const auto& transition : handler->get_observation().get_transitions())
    {
        ASSERT_TRUE(transition.realized_added_fluent_atom_indices.has_value());
        ASSERT_TRUE(transition.realized_deleted_fluent_atom_indices.has_value());

        const auto parent = state_repository.get_state(*state_repository.get_packed_state(transition.parent_state));
        const auto successor = state_repository.get_state(*state_repository.get_packed_state(transition.successor_state));

        auto expected_added = iw::AtomIndexList {};
        auto expected_deleted = iw::AtomIndexList {};
        for (const auto atom_index : successor.get_atoms<FluentTag>())
        {
            if (!parent.get_atoms<FluentTag>().get(atom_index))
            {
                expected_added.push_back(atom_index);
            }
        }
        for (const auto atom_index : parent.get_atoms<FluentTag>())
        {
            if (!successor.get_atoms<FluentTag>().get(atom_index))
            {
                expected_deleted.push_back(atom_index);
            }
        }

        EXPECT_EQ(*transition.realized_added_fluent_atom_indices, expected_added);
        EXPECT_EQ(*transition.realized_deleted_fluent_atom_indices, expected_deleted);

        /* Realized changes are bounded by what the action can do syntactically: an add effect that
           was already true realizes nothing, but nothing can be realized that the action does not
           name. */
        ASSERT_NE(transition.action_effect_summary, nullptr);
        EXPECT_LE(transition.realized_added_fluent_atom_indices->size(), transition.action_effect_summary->num_add_effects);
        EXPECT_LE(transition.realized_deleted_fluent_atom_indices->size(), transition.action_effect_summary->num_delete_effects);
        ++num_checked;
    }

    EXPECT_GT(num_checked, 0u);
}

TEST(MimirTests, RichObservationAggregatesSummarizeTheTransitionLogTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    auto options = all_transitions_options();
    options.capture_novel_witnesses = true;
    options.capture_action_effect_summaries = true;
    options.capture_realized_effects = true;
    const auto handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), options);

    ASSERT_NE(brfs::find_solution(context, make_projective_iw1_options(context->get_problem(), handler)).status, SearchStatus::FAILED);

    const auto& transitions = handler->get_observation().get_transitions();
    const auto aggregates = brfs::compute_transition_aggregates(transitions);

    EXPECT_EQ(aggregates.num_transitions, transitions.size());
    EXPECT_EQ(aggregates.num_admitted + aggregates.num_rejected, aggregates.num_transitions);
    EXPECT_EQ(aggregates.num_admitted, handler->get_statistics().get_num_generated_in_search_tree());
    EXPECT_EQ(aggregates.num_rejected, handler->get_statistics().get_num_generated_not_in_search_tree());

    auto expected_witness_total = uint64_t(0);
    auto expected_max_witness = uint32_t(0);
    for (const auto& transition : transitions)
    {
        if (transition.novel_fluent_atom_indices.has_value())
        {
            expected_witness_total += transition.novel_fluent_atom_indices->size();
            expected_max_witness = std::max(expected_max_witness, static_cast<uint32_t>(transition.novel_fluent_atom_indices->size()));
        }
    }
    EXPECT_EQ(aggregates.total_witness_size, expected_witness_total);
    EXPECT_EQ(aggregates.max_witness_size, expected_max_witness);

    auto summed_admitted_by_depth = uint64_t(0);
    for (const auto count : aggregates.num_admitted_by_depth)
    {
        summed_admitted_by_depth += count;
    }
    EXPECT_EQ(summed_admitted_by_depth, aggregates.num_admitted);

    auto summed_rejected_by_depth = uint64_t(0);
    for (const auto count : aggregates.num_rejected_by_depth)
    {
        summed_rejected_by_depth += count;
    }
    EXPECT_EQ(summed_rejected_by_depth, aggregates.num_rejected);
}

/// Fanning an event out to two observers must leave each one counting exactly what it would have
/// counted alone.
TEST(MimirTests, RichObservationCompositeDoesNotDoubleCountTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    const auto solo_handler = brfs::DefaultEventHandlerImpl::create(context->get_problem());
    ASSERT_NE(brfs::find_solution(context, make_projective_iw1_options(context->get_problem(), solo_handler)).status, SearchStatus::FAILED);
    const auto solo_statistics = solo_handler->get_statistics();

    const auto composed_native = brfs::ObservationEventHandlerImpl::create(context->get_problem(), all_transitions_options());
    auto composed_second = std::make_shared<WitnessRecordingEventHandlerImpl>();
    const auto composite = brfs::CompositeEventHandlerImpl::create({ composed_native, composed_second }, 0);
    ASSERT_NE(brfs::find_solution(context, make_projective_iw1_options(context->get_problem(), composite)).status, SearchStatus::FAILED);

    const auto& composed_statistics = composed_native->get_statistics();
    EXPECT_EQ(composed_statistics.get_num_generated(), solo_statistics.get_num_generated());
    EXPECT_EQ(composed_statistics.get_num_generated_in_search_tree(), solo_statistics.get_num_generated_in_search_tree());
    EXPECT_EQ(composed_statistics.get_num_generated_not_in_search_tree(), solo_statistics.get_num_generated_not_in_search_tree());
    EXPECT_EQ(composed_statistics.get_num_expanded(), solo_statistics.get_num_expanded());

    // The composite reports the nominated child's statistics, not a set of its own.
    EXPECT_EQ(composite->get_statistics().get_num_generated(), composed_statistics.get_num_generated());

    // Both children saw every event once.
    EXPECT_EQ(composed_second->admitted_transitions.size(), composed_statistics.get_num_generated_in_search_tree());
    EXPECT_EQ(composed_second->rejected_transitions.size(), composed_statistics.get_num_generated_not_in_search_tree());
}

TEST(MimirTests, RichObservationCompositeCombinesCapabilitiesTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    auto quiet_options = brfs::ObservationOptions {};
    quiet_options.capture_search_tree = true;
    const auto quiet_child = brfs::ObservationEventHandlerImpl::create(context->get_problem(), quiet_options);

    auto witness_options = quiet_options;
    witness_options.capture_novel_witnesses = true;
    const auto witness_child = brfs::ObservationEventHandlerImpl::create(context->get_problem(), witness_options);

    // Witnesses: any child that wants them gets them.
    EXPECT_TRUE(brfs::CompositeEventHandlerImpl::create({ quiet_child, witness_child }, 0)->supports_novel_witness_events());
    EXPECT_FALSE(brfs::CompositeEventHandlerImpl::create({ quiet_child, quiet_child }, 0)->supports_novel_witness_events());

    // Payloadless events: only if every child can work without payload.
    EXPECT_TRUE(brfs::CompositeEventHandlerImpl::create({ quiet_child, quiet_child }, 0)->supports_payloadless_generated_state_events());
    EXPECT_FALSE(brfs::CompositeEventHandlerImpl::create({ quiet_child, witness_child }, 0)->supports_payloadless_generated_state_events());
}

TEST(MimirTests, RichObservationPathReconstructionIsLazyAndCorrectTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    auto options = brfs::ObservationOptions {};
    options.capture_search_tree = true;
    const auto handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), options);

    auto search_options = brfs::Options {};
    search_options.event_handler = handler;
    const auto result = brfs::find_solution(context, search_options);
    ASSERT_EQ(result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(result.goal_state.has_value());

    const auto& tree = handler->get_observation().get_search_tree();

    EXPECT_TRUE(tree.get_action_indices(0).empty());
    EXPECT_EQ(tree.get_state_indices(0), std::vector<Index> { tree.get_nodes().front().state });

    for (size_t node_index = 0; node_index < tree.get_num_nodes(); ++node_index)
    {
        const auto actions = tree.get_action_indices(node_index);
        const auto states = tree.get_state_indices(node_index);

        EXPECT_EQ(states.size(), actions.size() + 1) << "node=" << node_index;
        EXPECT_EQ(states.front(), tree.get_nodes().front().state) << "node=" << node_index;
        EXPECT_EQ(states.back(), tree.get_nodes()[node_index].state) << "node=" << node_index;
        EXPECT_EQ(actions.size(), tree.get_nodes()[node_index].depth) << "node=" << node_index;
    }

    // Following parent indices reproduces the search's own predecessor relation.
    const auto goal_node = tree.find_node_by_state(result.goal_state.value().get_index());
    ASSERT_TRUE(goal_node.has_value());
    auto expected_actions = std::vector<Index> {};
    for (const auto action : result.plan.value().get_actions())
    {
        expected_actions.push_back(action->get_index());
    }
    EXPECT_EQ(tree.get_action_indices(*goal_node), expected_actions);

    EXPECT_THROW((void) tree.get_action_indices(tree.get_num_nodes()), std::out_of_range);
    EXPECT_THROW((void) tree.get_state_indices(tree.get_num_nodes()), std::out_of_range);
}

TEST(MimirTests, RichObservationMultiArityKeepsOnePassPerArityTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    const auto brfs_handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), all_transitions_options());
    const auto iw_handler = iw::ObservationEventHandlerImpl::create(context->get_problem(), brfs_handler);

    auto options = iw::Options {};
    options.brfs_event_handler = brfs_handler;
    options.iw_event_handler = iw_handler;
    options.max_arity = 2;

    const auto result = iw::find_solution(context, options);
    ASSERT_NE(result.status, SearchStatus::FAILED);

    const auto& by_arity = iw_handler->get_observation().get_by_arity();
    ASSERT_EQ(by_arity.size(), 3u) << "arities 0, 1 and 2 were attempted";

    for (size_t index = 0; index < by_arity.size(); ++index)
    {
        EXPECT_EQ(by_arity[index].arity, index);

        // Each pass's statistics describe the same pass as its tree and its transitions.
        const auto num_admitted = std::count_if(by_arity[index].observation.get_transitions().begin(),
                                                by_arity[index].observation.get_transitions().end(),
                                                [](const auto& t) { return t.disposition == brfs::TransitionDisposition::ADMITTED; });
        EXPECT_EQ(static_cast<uint64_t>(num_admitted), by_arity[index].statistics.get_num_generated_in_search_tree()) << "arity=" << index;
        EXPECT_EQ(by_arity[index].observation.get_transitions().size(), by_arity[index].statistics.get_num_generated()) << "arity=" << index;

        // Every tree is rooted at the start state and indexes its own nodes.
        if (by_arity[index].observation.get_search_tree().get_num_nodes() > 0)
        {
            EXPECT_FALSE(by_arity[index].observation.get_search_tree().get_nodes().front().parent_node.has_value()) << "arity=" << index;
        }
    }
}

/// Optimized IW(1) reports a width-0 pass that never runs a search. It stays in the list as an empty
/// entry so an entry's position keeps meaning its width.
TEST(MimirTests, RichObservationOptimizedIW1KeepsTheWidthZeroPlaceholderTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    const auto brfs_handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), all_transitions_options());
    const auto iw_handler = iw::ObservationEventHandlerImpl::create(context->get_problem(), brfs_handler);

    auto options = iw::Options {};
    options.brfs_event_handler = brfs_handler;
    options.iw_event_handler = iw_handler;
    options.max_arity = 1;

    ASSERT_NE(iw::find_solution(context, options).status, SearchStatus::SOLVED);

    const auto& by_arity = iw_handler->get_observation().get_by_arity();
    ASSERT_EQ(by_arity.size(), 2u);

    EXPECT_EQ(by_arity[0].arity, 0u);
    EXPECT_EQ(by_arity[0].observation.get_search_tree().get_num_nodes(), 0u) << "the width-0 pass ran no search";
    EXPECT_TRUE(by_arity[0].observation.get_transitions().empty());
    EXPECT_EQ(by_arity[0].statistics.get_num_generated(), 0u);

    EXPECT_EQ(by_arity[1].arity, 1u);
    EXPECT_GT(by_arity[1].observation.get_search_tree().get_num_nodes(), 0u);
    EXPECT_GT(by_arity[1].observation.get_transitions().size(), 0u);
}

/// A pass that stops early still contributes what it observed, and the passes before it are intact.
TEST(MimirTests, RichObservationSurvivesEarlyTerminationTest)
{
    const auto context = make_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    const auto brfs_handler = brfs::ObservationEventHandlerImpl::create(context->get_problem(), all_transitions_options());
    const auto iw_handler = iw::ObservationEventHandlerImpl::create(context->get_problem(), brfs_handler);

    auto options = iw::Options {};
    options.brfs_event_handler = brfs_handler;
    options.iw_event_handler = iw_handler;
    options.max_arity = 2;
    options.max_num_states = 3;  ///< stop partway through a pass

    const auto result = iw::find_solution(context, options);
    EXPECT_EQ(result.status, SearchStatus::OUT_OF_STATES);

    const auto& by_arity = iw_handler->get_observation().get_by_arity();
    ASSERT_FALSE(by_arity.empty()) << "the pass that stopped still reports what it saw";

    // Whatever was recorded is internally consistent, however early the search gave up.
    for (const auto& entry : by_arity)
    {
        const auto& transitions = entry.observation.get_transitions();
        EXPECT_LE(transitions.size(), entry.statistics.get_num_generated()) << "arity=" << entry.arity;
        for (const auto& transition : transitions)
        {
            EXPECT_EQ(transition.successor_depth, transition.parent_depth + 1);
        }
    }
}
}
