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

/// Abstracted LIW(k): the abstracted feature tuple paired with a landmark coordinate.
///
/// The two widenings are orthogonal. Abstraction changes what a tuple *is* -- object identities in
/// non-preserved argument slots become type signatures -- while the landmark coordinate changes
/// what a tuple is *indexed by*. So the combined family contains the abstracted one, and the tests
/// below are about that containment and about the ways it could be silently violated:
///
/// * an empty landmark set must collapse the family back onto abstracted IW(k), the same collapse
///   `LandmarkNoveltyTable` guarantees for plain LIW;
/// * every transition abstracted IW(k) admits must still be admitted;
/// * the read-only precheck must never under-approximate, since a `false` prunes a transition
///   before its successor is ever built -- and the flipped-rank half of it is deliberately
///   conservative rather than exact;
/// * the capabilities that cannot be answered under a landmark coordinate must report themselves
///   unsupported rather than answer wrongly.

#include "mimir/common/filesystem.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/algorithms/iw/pruning_strategy.hpp"
#include "mimir/search/applicable_action_generators.hpp"
#include "mimir/search/axiom_evaluators.hpp"
#include "mimir/search/grounders.hpp"
#include "mimir/search/landmarks/fact_landmark_generator.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <deque>
#include <gtest/gtest.h>
#include <string>
#include <unordered_set>
#include <vector>

using namespace mimir::formalism;
using namespace mimir::search;
using namespace mimir::search::landmarks;

namespace mimir::tests
{
namespace
{

struct Instance
{
    Problem problem;
    LiftedGrounder grounder;
    GroundedAxiomEvaluator axiom_evaluator;
    StateRepository state_repository;
    GroundedApplicableActionGenerator action_generator;
    FactLandmarkGraph landmarks;
    /// Goal seeding is the only seed source, so switching it off yields an empty landmark set.
    FactLandmarkGraph empty_landmarks;

    explicit Instance(const std::string& domain = "blocks_3", const std::string& instance = "test_problem.pddl") :
        problem(ProblemImpl::create(fs::path(std::string(DATA_DIR) + domain + "/domain.pddl"), fs::path(std::string(DATA_DIR) + domain + "/" + instance))),
        grounder(problem),
        axiom_evaluator(grounder.create_grounded_axiom_evaluator()),
        state_repository(StateRepositoryImpl::create(axiom_evaluator)),
        action_generator(grounder.create_grounded_applicable_action_generator()),
        landmarks(ApproximateFactLandmarkGenerator::create(grounder)),
        empty_landmarks(nullptr)
    {
        auto options = FactLandmarkGeneratorOptions {};
        options.include_positive_goal_facts = false;
        empty_landmarks = ApproximateFactLandmarkGenerator::create(grounder, options);
    }
};

struct Transition
{
    State state;
    State succ_state;
    iw::AtomIndexList add_atom_indices;
    iw::AtomIndexList del_atom_indices;
};

/// @brief Breadth-first sweep collecting up to `max_transitions` transitions in generation order,
/// with their atom-level delta, so a strategy can be replayed against exactly the same sequence.
std::vector<Transition> collect_transitions(Instance& instance, size_t max_transitions)
{
    const auto [initial_state, initial_g] = instance.state_repository->get_or_create_initial_state();

    auto transitions = std::vector<Transition> {};
    auto queue = std::deque<std::pair<State, ContinuousCost>> {};
    auto seen = std::unordered_set<Index> {};

    queue.emplace_back(initial_state, initial_g);
    seen.insert(initial_state.get_index());

    while (!queue.empty() && transitions.size() < max_transitions)
    {
        const auto [state, g_value] = queue.front();
        queue.pop_front();

        for (const auto& action : instance.action_generator->create_applicable_action_generator(state))
        {
            const auto [succ_state, succ_g_value] = instance.state_repository->get_or_create_successor_state(state, action, g_value);

            auto transition = Transition { state, succ_state, {}, {} };
            const auto& state_atoms = state.get_atoms<FluentTag>();
            const auto& succ_atoms = succ_state.get_atoms<FluentTag>();
            for (const auto atom_index : succ_atoms)
            {
                if (!state_atoms.get(atom_index))
                {
                    transition.add_atom_indices.push_back(atom_index);
                }
            }
            for (const auto atom_index : state_atoms)
            {
                if (!succ_atoms.get(atom_index))
                {
                    transition.del_atom_indices.push_back(atom_index);
                }
            }
            transitions.push_back(std::move(transition));

            if (seen.insert(succ_state.get_index()).second)
            {
                queue.emplace_back(succ_state, succ_g_value);
            }
            if (transitions.size() >= max_transitions)
            {
                break;
            }
        }
    }

    return transitions;
}

using Strategy = iw::AbstractedNoveltyPruningStrategyImpl;

std::unique_ptr<Strategy> make_strategy(const Instance& instance, size_t width, FactLandmarkGraph landmarks, iw::LandmarkGrouping grouping = {})
{
    return std::make_unique<Strategy>(instance.problem, width, false, true, false, std::move(landmarks), std::move(grouping));
}

/// @brief Which transitions of `transitions` the strategy admits, in order.
std::vector<bool> admitted(Strategy& strategy, const std::vector<Transition>& transitions)
{
    auto verdicts = std::vector<bool> {};
    verdicts.reserve(transitions.size());

    auto seen = std::unordered_set<Index> {};
    strategy.test_prune_initial_state(transitions.front().state);
    seen.insert(transitions.front().state.get_index());

    for (const auto& transition : transitions)
    {
        const auto is_new_succ = seen.insert(transition.succ_state.get_index()).second;
        verdicts.push_back(!strategy.test_prune_successor_state(transition.state, transition.succ_state, is_new_succ));
    }
    return verdicts;
}

}

TEST(MimirTests, SearchAlgorithmsAbstractedLandmarkNoveltyIsOffByDefault)
{
    auto instance = Instance {};
    const auto plain = make_strategy(instance, 1, nullptr);

    EXPECT_FALSE(plain->is_landmark_restricted());
    EXPECT_EQ(plain->get_num_landmark_ranks(), 1u) << "without a graph there is exactly one novelty table";
    EXPECT_FALSE(plain->precheck_requires_delete_effects()) << "abstracted features are atom-level; deletes cannot create novelty";
}

TEST(MimirTests, SearchAlgorithmsAbstractedLandmarkNoveltyReportsItsRanks)
{
    auto instance = Instance {};
    const auto landmark = make_strategy(instance, 1, instance.landmarks);

    ASSERT_FALSE(instance.landmarks->get_landmark_atom_indices().empty());
    EXPECT_TRUE(landmark->is_landmark_restricted());
    // One rank per landmark atom, plus the BOT rank states satisfying none take.
    EXPECT_EQ(landmark->get_num_landmark_ranks(), instance.landmarks->get_landmark_atom_indices().size() + 1);
    EXPECT_TRUE(landmark->precheck_requires_delete_effects()) << "a landmark rank goes off with its last true carrier";
}

TEST(MimirTests, SearchAlgorithmsAbstractedLandmarkNoveltyWithoutLandmarksCollapses)
{
    /* The collapse that makes the coordinate a widening rather than a different family: with no
       landmark atoms, BOT is every state's only coordinate and the feature family is exactly
       abstracted IW(k)'s. Checked on the admitted sequence, not just on a count, because two
       different families can admit the same NUMBER of transitions. */
    auto instance = Instance {};
    ASSERT_TRUE(instance.empty_landmarks->get_landmark_atom_indices().empty());
    const auto transitions = collect_transitions(instance, 400);
    ASSERT_FALSE(transitions.empty());

    for (const auto width : { size_t(1), size_t(2) })
    {
        auto plain = make_strategy(instance, width, nullptr);
        auto empty_landmark = make_strategy(instance, width, instance.empty_landmarks);
        EXPECT_EQ(admitted(*plain, transitions), admitted(*empty_landmark, transitions)) << "width " << width;
    }
}

TEST(MimirTests, SearchAlgorithmsAbstractedLandmarkNoveltyAdmitsEverythingAbstractedIWAdmits)
{
    /* The containment that justifies the combination. An abstracted feature tuple that is novel
       under no coordinate is novel under the state's coordinate too, so the landmark-restricted
       family can only admit more -- never fewer. A regression here is the dangerous direction: the
       search would silently lose transitions plain abstracted IW(k) would have taken. */
    auto instance = Instance {};
    const auto transitions = collect_transitions(instance, 400);
    ASSERT_FALSE(transitions.empty());

    for (const auto width : { size_t(1), size_t(2) })
    {
        auto plain = make_strategy(instance, width, nullptr);
        auto landmark = make_strategy(instance, width, instance.landmarks);

        const auto plain_verdicts = admitted(*plain, transitions);
        const auto landmark_verdicts = admitted(*landmark, transitions);

        auto num_extra = size_t(0);
        for (size_t i = 0; i < transitions.size(); ++i)
        {
            EXPECT_TRUE(landmark_verdicts[i] || !plain_verdicts[i])
                << "width " << width << ": transition " << i << " admitted by abstracted IW but rejected by abstracted LIW";
            num_extra += (landmark_verdicts[i] && !plain_verdicts[i]) ? 1 : 0;
        }
        EXPECT_GT(num_extra, 0u) << "width " << width << ": the landmark coordinate admitted nothing extra, so it is inert here";
    }
}

TEST(MimirTests, SearchAlgorithmsAbstractedLandmarkNoveltyPrecheckNeverUnderApproximates)
{
    /* The precheck answers "may this transition be novel?" before the successor is built, so a
       false prunes it unseen. It is allowed to say true too often -- the flipped-rank half
       deliberately does, since answering exactly would mean reconstructing the successor -- but
       never too rarely. Replayed against a second strategy fed the identical sequence, so both
       tables hold the same marks when each transition is asked about. */
    auto instance = Instance {};
    const auto transitions = collect_transitions(instance, 400);
    ASSERT_FALSE(transitions.empty());

    auto oracle = make_strategy(instance, 1, instance.landmarks);
    auto probe = make_strategy(instance, 1, instance.landmarks);
    ASSERT_TRUE(probe->supports_action_add_effect_precheck());

    auto seen = std::unordered_set<Index> {};
    oracle->test_prune_initial_state(transitions.front().state);
    probe->test_prune_initial_state(transitions.front().state);
    seen.insert(transitions.front().state.get_index());

    auto num_novel = size_t(0);
    for (const auto& transition : transitions)
    {
        const auto precheck =
            probe->test_transition_novelty_from_add_effects(transition.state, transition.add_atom_indices, transition.del_atom_indices);
        const auto is_new_succ = seen.insert(transition.succ_state.get_index()).second;
        const auto is_novel = !oracle->test_prune_successor_state(transition.state, transition.succ_state, is_new_succ);
        // Keep the probe's tables in step with the oracle's.
        probe->test_prune_successor_state(transition.state, transition.succ_state, is_new_succ);

        if (is_novel)
        {
            ++num_novel;
            EXPECT_TRUE(precheck) << "the precheck would have pruned a novel transition unseen";
        }
    }
    EXPECT_GT(num_novel, 0u) << "no transition was novel, so the assertion above never fired";
}

TEST(MimirTests, SearchAlgorithmsAbstractedLandmarkNoveltyRefusesBeamModes)
{
    /* Unsupported rather than wrong. The staged beam entry points are handed a raw successor
       bitset instead of a `State`, which is not the query a landmark coordinate can answer, so
       claiming support would score the beam under a different feature family without saying so. */
    auto instance = Instance {};
    const auto plain = make_strategy(instance, 1, nullptr);
    const auto landmark = make_strategy(instance, 1, instance.landmarks);

    for (const auto mode : { BeamNoveltyMode::ALL_TESTED, BeamNoveltyMode::SURVIVORS_ONLY })
    {
        EXPECT_TRUE(plain->supports_beam_novelty_mode(mode));
        EXPECT_FALSE(landmark->supports_beam_novelty_mode(mode));
        EXPECT_FALSE(landmark->supports_staged_beam_pruning(mode));
    }
}

TEST(MimirTests, SearchAlgorithmsAbstractedLandmarkNoveltyWitnessNamesOnlyAddedAtoms)
{
    /* The witness query names an ATOM the caller can route on, and only an added atom can be one:
       a rank that flipped on makes the whole successor novel without naming anything. */
    auto instance = Instance {};
    const auto transitions = collect_transitions(instance, 200);
    ASSERT_FALSE(transitions.empty());

    auto landmark = make_strategy(instance, 1, instance.landmarks);
    ASSERT_TRUE(landmark->supports_transition_novel_witness_query());
    landmark->test_prune_initial_state(transitions.front().state);

    auto witnesses = iw::AtomIndexList {};
    auto num_witnessed = size_t(0);
    for (const auto& transition : transitions)
    {
        landmark->compute_transition_novel_fluent_atom_indices_read_only(transition.state, transition.succ_state, witnesses);
        for (const auto atom_index : witnesses)
        {
            EXPECT_FALSE(transition.state.get_atoms<FluentTag>().get(atom_index)) << "a witness must be an atom the transition added";
            EXPECT_TRUE(transition.succ_state.get_atoms<FluentTag>().get(atom_index));
        }
        num_witnessed += witnesses.empty() ? 0 : 1;
    }
    EXPECT_GT(num_witnessed, 0u);
}

TEST(MimirTests, SearchAlgorithmsAbstractedLandmarkNoveltySharedRanksPruneAtLeastAsHard)
{
    /* Sharing a novelty row between the members of a disjunctive landmark means whichever member
       is reached first pays the exploration for all of them, so the shared family is a coarsening:
       it can only admit fewer transitions than the same graph with private rows. */
    auto instance = Instance {};
    const auto transitions = collect_transitions(instance, 400);
    ASSERT_FALSE(transitions.empty());

    const auto& landmark_atoms = instance.landmarks->get_landmark_atom_indices();
    ASSERT_GE(landmark_atoms.size(), 2u);

    auto grouping = iw::LandmarkGrouping {};
    grouping.disjunctive_landmarks.push_back(iw::AtomIndexList { landmark_atoms[0], landmark_atoms[1] });

    auto private_rows = make_strategy(instance, 1, instance.landmarks);
    auto shared_rows = make_strategy(instance, 1, instance.landmarks, grouping);

    const auto private_verdicts = admitted(*private_rows, transitions);
    const auto shared_verdicts = admitted(*shared_rows, transitions);

    for (size_t i = 0; i < transitions.size(); ++i)
    {
        EXPECT_TRUE(private_verdicts[i] || !shared_verdicts[i]) << "sharing rows admitted transition " << i << " that private rows rejected";
    }
}

}
