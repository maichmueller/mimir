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

#include <algorithm>
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

std::unique_ptr<Strategy> make_strategy(const Instance& instance,
                                        size_t width,
                                        FactLandmarkGraph landmarks,
                                        iw::LandmarkGrouping grouping = {},
                                        bool preserve_landmark_atoms = true)
{
    return std::make_unique<Strategy>(instance.problem, width, false, true, false, std::move(landmarks), std::move(grouping), preserve_landmark_atoms);
}

/// @brief How many of `verdicts` are admissions.
size_t count_admitted(const std::vector<bool>& verdicts) { return static_cast<size_t>(std::count(verdicts.begin(), verdicts.end(), true)); }

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

TEST(MimirTests, SearchAlgorithmsAbstractedLandmarkNoveltyAllPrivateMatchesExplicitUnsharedUnion)
{
    auto instance = Instance {};
    const auto transitions = collect_transitions(instance, 400);
    ASSERT_FALSE(transitions.empty());

    const auto& landmark_atoms = instance.landmarks->get_landmark_atom_indices();
    ASSERT_GE(landmark_atoms.size(), 2u);

    auto explicit_unshared = iw::LandmarkGrouping {};
    explicit_unshared.disjunctive_landmarks.push_back(iw::AtomIndexList { landmark_atoms[0], landmark_atoms[1] });
    explicit_unshared.unshared_atom_indices.insert(landmark_atoms[0]);
    explicit_unshared.unshared_atom_indices.insert(landmark_atoms[1]);

    auto all_private = iw::LandmarkGrouping {};
    all_private.disjunctive_landmarks = explicit_unshared.disjunctive_landmarks;
    all_private.mode = iw::LandmarkGroupingMode::ALL_PRIVATE;

    auto explicit_strategy = make_strategy(instance, 1, instance.landmarks, explicit_unshared);
    auto all_private_strategy = make_strategy(instance, 1, instance.landmarks, all_private);
    EXPECT_EQ(explicit_strategy->get_num_landmark_ranks(), all_private_strategy->get_num_landmark_ranks());
    EXPECT_EQ(admitted(*explicit_strategy, transitions), admitted(*all_private_strategy, transitions));
}


TEST(MimirTests, SearchAlgorithmsAbstractedLandmarkNoveltyPreservesLandmarkAtomsByDefault)
{
    /* The exemption is on unless asked otherwise, and it covers exactly the atoms that carry a
       rank -- the set the coordinate itself is built from. */
    auto instance = Instance {};
    ASSERT_FALSE(instance.landmarks->get_landmark_atom_indices().empty());

    const auto on = make_strategy(instance, 1, instance.landmarks);
    EXPECT_EQ(on->get_num_preserved_landmark_atoms(), instance.landmarks->get_landmark_atom_indices().size());

    const auto off = make_strategy(instance, 1, instance.landmarks, {}, false);
    EXPECT_EQ(off->get_num_preserved_landmark_atoms(), 0u);
}

TEST(MimirTests, SearchAlgorithmsAbstractedLandmarkNoveltyPreservationNeedsLandmarksToDoAnything)
{
    /* Nothing to exempt without landmark atoms, so abstracted IW(k) stays byte-identical whatever
       the flag says -- the same collapse the coordinate itself guarantees. */
    auto instance = Instance {};
    const auto transitions = collect_transitions(instance, 400);
    ASSERT_FALSE(transitions.empty());

    for (const auto& landmarks : { FactLandmarkGraph(nullptr), instance.empty_landmarks })
    {
        const auto on = make_strategy(instance, 1, landmarks);
        EXPECT_EQ(on->get_num_preserved_landmark_atoms(), 0u);

        for (const auto width : { size_t(1), size_t(2) })
        {
            auto preserved = make_strategy(instance, width, landmarks);
            auto unpreserved = make_strategy(instance, width, landmarks, {}, false);
            EXPECT_EQ(admitted(*preserved, transitions), admitted(*unpreserved, transitions)) << "width " << width;
        }
    }
}

TEST(MimirTests, SearchAlgorithmsAbstractedLandmarkNoveltyPreservationIsNotInert)
{
    /* The point of the option. `transport`'s landmark set reaches well past its goal facts, and its
       landmark atoms are the kind abstraction folds together -- same predicate, same type signature,
       different objects -- so exempting them moves the admitted sequence.
       One named instance rather than a sweep: the exemption only bites where every abstracted
       feature of an atom is already seen at that atom's rank while the atom itself is not, and most
       small instances never get there. */
    auto instance = Instance { "transport" };
    ASSERT_FALSE(instance.landmarks->get_landmark_atom_indices().empty());
    const auto transitions = collect_transitions(instance, 1000);
    ASSERT_FALSE(transitions.empty());

    auto preserved = make_strategy(instance, 2, instance.landmarks);
    auto unpreserved = make_strategy(instance, 2, instance.landmarks, {}, false);

    const auto preserved_verdicts = admitted(*preserved, transitions);
    const auto unpreserved_verdicts = admitted(*unpreserved, transitions);
    EXPECT_NE(preserved_verdicts, unpreserved_verdicts) << "the exemption changed nothing, so it is not reaching the features";
    EXPECT_GT(count_admitted(preserved_verdicts), count_admitted(unpreserved_verdicts));
}

TEST(MimirTests, SearchAlgorithmsAbstractedLandmarkNoveltyPreservationNeverAdmitsLess)
{
    /* Exempting an atom only ever adds a feature to it -- below arity two it swaps the one
       abstracted feature for the full one, the same partition of atoms under a different key -- so
       it can only hand transitions more chances to be novel.
       Totals rather than a per-transition inclusion: once the two families disagree they fill their
       tables differently, so an individual transition the coarser one admits can lose its witness
       under the finer one. The totals are the invariant a regression would break. */
    for (const auto* domain : { "blocks_3", "delivery", "gripper", "ferry", "transport" })
    {
        auto instance = Instance { domain };
        const auto transitions = collect_transitions(instance, 1000);
        ASSERT_FALSE(transitions.empty()) << domain;

        for (const auto width : { size_t(1), size_t(2) })
        {
            auto preserved = make_strategy(instance, width, instance.landmarks);
            auto unpreserved = make_strategy(instance, width, instance.landmarks, {}, false);
            EXPECT_GE(count_admitted(admitted(*preserved, transitions)), count_admitted(admitted(*unpreserved, transitions)))
                << domain << " width " << width;
        }
    }
}
}
