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

#include "mimir/search/algorithms/iw/landmark_novelty_table.hpp"

#include "mimir/common/filesystem.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/parser.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/search/algorithms/iw/novelty_table.hpp"
#include "mimir/search/applicable_action_generators.hpp"
#include "mimir/search/axiom_evaluators.hpp"
#include "mimir/search/grounders.hpp"
#include "mimir/search/landmarks/fact_landmark_generator.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state.hpp"
#include "mimir/search/state_repository.hpp"

#include <deque>
#include <gtest/gtest.h>
#include <unordered_set>
#include <vector>

using namespace mimir::formalism;
using namespace mimir::search;
using namespace mimir::search::landmarks;

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
    FactLandmarkGraph landmarks;

    explicit Fixture(std::string domain = "blocks_3", std::string instance = "test_problem.pddl") :
        problem(ProblemImpl::create(fs::path(std::string(DATA_DIR) + domain + "/domain.pddl"), fs::path(std::string(DATA_DIR) + domain + "/" + instance))),
        grounder(problem),
        axiom_evaluator(grounder.create_grounded_axiom_evaluator()),
        state_repository(StateRepositoryImpl::create(axiom_evaluator)),
        action_generator(grounder.create_grounded_applicable_action_generator()),
        landmarks(ApproximateFactLandmarkGenerator::create(grounder))
    {
    }
};

/// @brief A (predecessor, successor) pair as produced by a breadth-first sweep of the state space.
struct Transition
{
    State state;
    State succ_state;
};

/// @brief Breadth-first sweep collecting up to `max_transitions` transitions in generation order.
/// Every generated transition is collected, including those leading to already-seen states, so the
/// resulting sequence is exactly what an unpruned search would feed to a novelty table.
std::vector<Transition> collect_transitions(Fixture& fixture, size_t max_transitions)
{
    const auto [initial_state, initial_g] = fixture.state_repository->get_or_create_initial_state();

    auto transitions = std::vector<Transition> {};
    auto queue = std::deque<std::pair<State, ContinuousCost>> {};
    auto seen = std::unordered_set<Index> {};

    queue.emplace_back(initial_state, initial_g);
    seen.insert(initial_state.get_index());

    while (!queue.empty() && transitions.size() < max_transitions)
    {
        const auto [state, g_value] = queue.front();
        queue.pop_front();

        for (const auto& action : fixture.action_generator->create_applicable_action_generator(state))
        {
            const auto [succ_state, succ_g_value] = fixture.state_repository->get_or_create_successor_state(state, action, g_value);
            transitions.push_back(Transition { state, succ_state });

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

iw::AtomIndexList landmark_atom_indices(const Fixture& fixture)
{
    const auto& indices = fixture.landmarks->get_landmark_atom_indices();
    return iw::AtomIndexList(indices.begin(), indices.end());
}

bool has_true_landmark(const Fixture& fixture, const State& state)
{
    const auto& fluent_atoms = state.get_atoms<FluentTag>();
    for (const auto atom_index : fixture.landmarks->get_landmark_atom_indices())
    {
        if (fluent_atoms.get(atom_index))
        {
            return true;
        }
    }
    return false;
}

}

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableRanksLandmarks)
{
    auto fixture = Fixture {};
    const auto landmark_atoms = landmark_atom_indices(fixture);
    ASSERT_FALSE(landmark_atoms.empty());

    auto table = iw::LandmarkNoveltyTable(landmark_atoms, 1);

    EXPECT_EQ(table.get_num_landmarks(), landmark_atoms.size());
    EXPECT_EQ(table.get_bot_rank(), landmark_atoms.size());

    // Ranks are dense, distinct and follow the (sorted) landmark atom order.
    auto seen_ranks = std::unordered_set<uint32_t> {};
    for (const auto atom_index : landmark_atoms)
    {
        const auto rank = table.get_landmark_rank(atom_index);
        ASSERT_NE(rank, iw::LandmarkNoveltyTable::NOT_A_LANDMARK);
        EXPECT_LT(rank, table.get_bot_rank());
        EXPECT_TRUE(seen_ranks.insert(rank).second);
    }

    // A non-landmark atom index has no rank. Landmark atom indices are bounded by the number of
    // ground atoms, so an index far beyond them cannot be one.
    EXPECT_EQ(table.get_landmark_rank(landmark_atoms.back() + 1000), iw::LandmarkNoveltyTable::NOT_A_LANDMARK);
}

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableUsesBotOnlyWithoutTrueLandmark)
{
    auto fixture = Fixture {};
    const auto landmark_atoms = landmark_atom_indices(fixture);
    auto table = iw::LandmarkNoveltyTable(landmark_atoms, 1);

    auto ranks = std::vector<uint32_t> {};
    for (const auto& transition : collect_transitions(fixture, 200))
    {
        table.collect_landmark_ranks(transition.succ_state, ranks);
        ASSERT_FALSE(ranks.empty()) << "the landmark coordinate must be total";

        const auto uses_bot = (ranks.size() == 1) && (ranks.front() == table.get_bot_rank());
        EXPECT_EQ(uses_bot, !has_true_landmark(fixture, transition.succ_state));
    }
}

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableWithoutLandmarksMatchesPlainTable)
{
    // With an empty landmark set every state falls back to BOT, so the table must reduce to
    // exactly `DynamicNoveltyTable` of the same arity -- transition for transition.
    for (const size_t arity : { size_t(1), size_t(2) })
    {
        auto fixture = Fixture {};
        auto landmark_table = iw::LandmarkNoveltyTable(iw::AtomIndexList {}, arity);
        auto plain_table = iw::DynamicNoveltyTable(arity);

        const auto transitions = collect_transitions(fixture, 400);
        ASSERT_FALSE(transitions.empty());

        EXPECT_EQ(landmark_table.test_novelty_and_update_table(transitions.front().state),
                  plain_table.test_novelty_and_update_table(transitions.front().state));

        for (const auto& transition : transitions)
        {
            EXPECT_EQ(landmark_table.test_novelty_and_update_table(transition.state, transition.succ_state),
                      plain_table.test_novelty_and_update_table(transition.state, transition.succ_state))
                << "arity " << arity;
        }
    }
}

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableSubsumesPlainNovelty)
{
    // LIW(k) admits everything IW(k) admits: an unseen free tuple `t` implies an unseen pair
    // `(l, t)` for every landmark coordinate `l`, and the coordinate set is never empty.
    for (const size_t arity : { size_t(1), size_t(2) })
    {
        auto fixture = Fixture {};
        const auto landmark_atoms = landmark_atom_indices(fixture);
        ASSERT_FALSE(landmark_atoms.empty());

        auto landmark_table = iw::LandmarkNoveltyTable(landmark_atoms, arity);
        auto plain_table = iw::DynamicNoveltyTable(arity);

        const auto transitions = collect_transitions(fixture, 400);
        ASSERT_FALSE(transitions.empty());

        landmark_table.test_novelty_and_update_table(transitions.front().state);
        plain_table.test_novelty_and_update_table(transitions.front().state);

        for (const auto& transition : transitions)
        {
            const auto landmark_novel = landmark_table.test_novelty_and_update_table(transition.state, transition.succ_state);
            const auto plain_novel = plain_table.test_novelty_and_update_table(transition.state, transition.succ_state);

            if (plain_novel)
            {
                EXPECT_TRUE(landmark_novel) << "IW(" << arity << ")-novel transition rejected by LIW(" << arity << ")";
            }
        }
    }
}

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableIsSubsumedByWiderPlainNovelty)
{
    // LIW(k) tuples are IW(k+1) tuples with the first coordinate pinned to a landmark, so an
    // LIW(k)-novel transition is IW(k+1)-novel. The BOT coordinate is the one exception: it is not
    // an atom, and its table region is disjoint from every real landmark's, so transitions whose
    // successor holds no landmark at all are excluded from the claim.
    for (const size_t arity : { size_t(1), size_t(2) })
    {
        auto fixture = Fixture {};
        const auto landmark_atoms = landmark_atom_indices(fixture);
        ASSERT_FALSE(landmark_atoms.empty());

        auto landmark_table = iw::LandmarkNoveltyTable(landmark_atoms, arity);
        auto wider_table = iw::DynamicNoveltyTable(arity + 1);

        const auto transitions = collect_transitions(fixture, 400);
        ASSERT_FALSE(transitions.empty());

        landmark_table.test_novelty_and_update_table(transitions.front().state);
        wider_table.test_novelty_and_update_table(transitions.front().state);

        for (const auto& transition : transitions)
        {
            const auto landmark_novel = landmark_table.test_novelty_and_update_table(transition.state, transition.succ_state);
            const auto wider_novel = wider_table.test_novelty_and_update_table(transition.state, transition.succ_state);

            if (landmark_novel && has_true_landmark(fixture, transition.succ_state) && has_true_landmark(fixture, transition.state))
            {
                EXPECT_TRUE(wider_novel) << "LIW(" << arity << ")-novel transition rejected by IW(" << arity + 1 << ")";
            }
        }
    }
}

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableRepeatsAreNotNovel)
{
    auto fixture = Fixture {};
    const auto landmark_atoms = landmark_atom_indices(fixture);
    auto table = iw::LandmarkNoveltyTable(landmark_atoms, 1);

    const auto transitions = collect_transitions(fixture, 200);
    ASSERT_FALSE(transitions.empty());

    for (const auto& transition : transitions)
    {
        table.test_novelty_and_update_table(transition.state, transition.succ_state);
    }
    // Replaying the very same transitions cannot expose an unseen feature.
    for (const auto& transition : transitions)
    {
        EXPECT_FALSE(table.test_novelty_and_update_table(transition.state, transition.succ_state));
    }
}

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableResizePreservesMarks)
{
    // Constructed with no atom capacity at all, the table must grow as states arrive without ever
    // losing a mark: after the sweep, replaying it must report nothing novel.
    auto fixture = Fixture {};
    const auto landmark_atoms = landmark_atom_indices(fixture);
    ASSERT_FALSE(landmark_atoms.empty());

    auto table = iw::LandmarkNoveltyTable(landmark_atoms, 2, 0);
    const auto transitions = collect_transitions(fixture, 300);
    ASSERT_FALSE(transitions.empty());

    for (const auto& transition : transitions)
    {
        table.test_novelty_and_update_table(transition.state, transition.succ_state);
    }
    EXPECT_GT(table.get_table_size(), (landmark_atoms.size() + 1));

    for (const auto& transition : transitions)
    {
        EXPECT_FALSE(table.test_novelty_read_only(transition.state, transition.succ_state));
    }
}

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableChoosesLayoutFromBudget)
{
    // `fits_dense` is what decides the layout, so pin its arithmetic directly: one bit per
    // (rank, free tuple) cell, and a hard representational ceiling that `force_dense` cannot lift.
    auto options = iw::LandmarkNoveltyTableOptions {};

    // 4 ranks x 101 tuples = 404 cells = 51 bytes.
    options.max_dense_table_bytes = 64;
    EXPECT_TRUE(iw::LandmarkNoveltyTable::fits_dense(4, 100, 1, options));
    options.max_dense_table_bytes = 32;
    EXPECT_FALSE(iw::LandmarkNoveltyTable::fits_dense(4, 100, 1, options));

    // Zero means "never dense"; force_dense overrides any budget, including zero.
    options.max_dense_table_bytes = 0;
    EXPECT_FALSE(iw::LandmarkNoveltyTable::fits_dense(1, 1, 1, options));
    options.force_dense = true;
    EXPECT_TRUE(iw::LandmarkNoveltyTable::fits_dense(4, 100, 1, options));

    // ... but not the representational ceiling: (10^6 + 1)^2 exceeds what a TupleIndex can address.
    EXPECT_FALSE(iw::LandmarkNoveltyTable::fits_dense(1, 1000000, 2, options));

    // The default budget keeps a realistic width-1 table dense.
    EXPECT_TRUE(iw::LandmarkNoveltyTable::fits_dense(50, 100000, 1, iw::LandmarkNoveltyTableOptions {}));
}

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableSparseLayoutAgreesWithDense)
{
    /* The layout is an implementation detail and must stay one: a table forced sparse by a zero
       budget has to give the same verdict on every transition as the dense one, including across
       the resizes that remap tuple indices. */
    for (const size_t arity : { size_t(1), size_t(2) })
    {
        auto fixture = Fixture {};
        const auto landmark_atoms = landmark_atom_indices(fixture);
        ASSERT_FALSE(landmark_atoms.empty());

        auto dense_options = iw::LandmarkNoveltyTableOptions {};
        dense_options.force_dense = true;
        auto sparse_options = iw::LandmarkNoveltyTableOptions {};
        sparse_options.max_dense_table_bytes = 0;

        auto dense_table = iw::LandmarkNoveltyTable(landmark_atoms, arity, 0, dense_options);
        auto sparse_table = iw::LandmarkNoveltyTable(landmark_atoms, arity, 0, sparse_options);
        ASSERT_TRUE(dense_table.is_dense());
        ASSERT_FALSE(sparse_table.is_dense());

        const auto transitions = collect_transitions(fixture, 400);
        ASSERT_FALSE(transitions.empty());

        EXPECT_EQ(dense_table.test_novelty_and_update_table(transitions.front().state), sparse_table.test_novelty_and_update_table(transitions.front().state));

        for (const auto& transition : transitions)
        {
            EXPECT_EQ(dense_table.test_novelty_and_update_table(transition.state, transition.succ_state),
                      sparse_table.test_novelty_and_update_table(transition.state, transition.succ_state))
                << "arity " << arity;
            EXPECT_EQ(dense_table.test_novelty_read_only(transition.state, transition.succ_state),
                      sparse_table.test_novelty_read_only(transition.state, transition.succ_state))
                << "arity " << arity;
        }
    }
}

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableSwitchesToSparseMidSearchWithoutLosingMarks)
{
    /* A budget that fits the initial table but not the grown one forces the one-way dense ->
       sparse switch during a resize. The marks must survive it: replaying the sweep afterwards
       must report nothing novel. */
    auto fixture = Fixture {};
    const auto landmark_atoms = landmark_atom_indices(fixture);
    ASSERT_FALSE(landmark_atoms.empty());

    auto options = iw::LandmarkNoveltyTableOptions {};
    options.max_dense_table_bytes = 64;

    auto table = iw::LandmarkNoveltyTable(landmark_atoms, 2, 0, options);
    ASSERT_TRUE(table.is_dense());

    const auto transitions = collect_transitions(fixture, 300);
    ASSERT_FALSE(transitions.empty());

    for (const auto& transition : transitions)
    {
        table.test_novelty_and_update_table(transition.state, transition.succ_state);
    }
    EXPECT_FALSE(table.is_dense()) << "the budget should have been outgrown";

    for (const auto& transition : transitions)
    {
        EXPECT_FALSE(table.test_novelty_read_only(transition.state, transition.succ_state));
    }
}

TEST(MimirTests, SearchAlgorithmsLandmarkMinimumGNoveltyTableWithoutLandmarksMatchesPlainTable)
{
    // The sparse landmark-restricted minimum-g table must reduce to the dense `MinimumGNoveltyTable`
    // when there are no landmarks -- same verdict on every transition, at every cost.
    for (const size_t arity : { size_t(1), size_t(2) })
    {
        auto fixture = Fixture {};
        auto landmark_table = iw::LandmarkMinimumGNoveltyTable(iw::AtomIndexList {}, arity);
        auto plain_table = iw::MinimumGNoveltyTable(arity);

        const auto transitions = collect_transitions(fixture, 400);
        ASSERT_FALSE(transitions.empty());

        EXPECT_EQ(landmark_table.test_novelty_and_update_table(transitions.front().state, ContinuousCost(0)),
                  plain_table.test_novelty_and_update_table(transitions.front().state, ContinuousCost(0)));

        auto g_value = ContinuousCost(1);
        for (const auto& transition : transitions)
        {
            EXPECT_EQ(landmark_table.test_novelty_and_update_table(transition.state, transition.succ_state, g_value),
                      plain_table.test_novelty_and_update_table(transition.state, transition.succ_state, g_value))
                << "arity " << arity;
            EXPECT_EQ(landmark_table.test_novelty_at_g_read_only(transition.succ_state, g_value),
                      plain_table.test_novelty_at_g_read_only(transition.succ_state, g_value))
                << "arity " << arity;
            g_value += 1;
        }
        EXPECT_EQ(landmark_table.has_lowered_existing_label(), plain_table.has_lowered_existing_label());
    }
}

TEST(MimirTests, SearchAlgorithmsLandmarkMinimumGNoveltyTableLowersLabels)
{
    auto fixture = Fixture {};
    const auto landmark_atoms = landmark_atom_indices(fixture);
    ASSERT_FALSE(landmark_atoms.empty());

    auto table = iw::LandmarkMinimumGNoveltyTable(landmark_atoms, 1);
    const auto transitions = collect_transitions(fixture, 50);
    ASSERT_FALSE(transitions.empty());
    const auto& transition = transitions.front();

    // Labelling for the first time is not a lowering: nothing can have been stolen.
    EXPECT_TRUE(table.test_novelty_and_update_table(transition.state, transition.succ_state, ContinuousCost(5)));
    EXPECT_FALSE(table.has_lowered_existing_label());
    EXPECT_TRUE(table.test_novelty_at_g_read_only(transition.succ_state, ContinuousCost(5)));

    // Re-reaching at a higher cost changes nothing.
    EXPECT_FALSE(table.test_novelty_and_update_table(transition.state, transition.succ_state, ContinuousCost(7)));
    EXPECT_FALSE(table.has_lowered_existing_label());
    EXPECT_FALSE(table.test_novelty_at_g_read_only(transition.succ_state, ContinuousCost(7)));

    // Re-reaching at a strictly smaller cost lowers the labels, and the old cost is gone.
    EXPECT_TRUE(table.test_novelty_and_update_table(transition.state, transition.succ_state, ContinuousCost(2)));
    EXPECT_TRUE(table.has_lowered_existing_label());
    EXPECT_TRUE(table.test_novelty_at_g_read_only(transition.succ_state, ContinuousCost(2)));
    EXPECT_FALSE(table.test_novelty_at_g_read_only(transition.succ_state, ContinuousCost(5)));
}

TEST(MimirTests, SearchAlgorithmsLandmarkMinimumGNoveltyTableResizePreservesLabels)
{
    // Constructed with no atom capacity, the table must grow as states arrive without losing a
    // label: replaying the sweep at the same costs must report no improvement.
    auto fixture = Fixture {};
    const auto landmark_atoms = landmark_atom_indices(fixture);
    ASSERT_FALSE(landmark_atoms.empty());

    auto table = iw::LandmarkMinimumGNoveltyTable(landmark_atoms, 2, 0);
    const auto transitions = collect_transitions(fixture, 300);
    ASSERT_FALSE(transitions.empty());

    for (const auto& transition : transitions)
    {
        table.test_novelty_and_update_table(transition.state, transition.succ_state, ContinuousCost(1));
    }
    EXPECT_GT(table.get_num_labelled_tuples(), 0u);

    for (const auto& transition : transitions)
    {
        EXPECT_FALSE(table.test_novelty_and_update_table(transition.state, transition.succ_state, ContinuousCost(1)));
    }
    EXPECT_FALSE(table.has_lowered_existing_label());
}

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableReadOnlyDoesNotUpdate)
{
    auto fixture = Fixture {};
    const auto landmark_atoms = landmark_atom_indices(fixture);
    auto table = iw::LandmarkNoveltyTable(landmark_atoms, 1);

    const auto transitions = collect_transitions(fixture, 50);
    ASSERT_FALSE(transitions.empty());

    const auto& transition = transitions.front();
    EXPECT_TRUE(table.test_novelty_read_only(transition.state, transition.succ_state));
    EXPECT_TRUE(table.test_novelty_read_only(transition.state, transition.succ_state));
    EXPECT_TRUE(table.test_novelty_and_update_table(transition.state, transition.succ_state));
    EXPECT_FALSE(table.test_novelty_read_only(transition.state, transition.succ_state));
}

}
