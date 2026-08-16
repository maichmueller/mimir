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
#include "mimir/search/algorithms/iw/tuple_index_generators.hpp"
#include "mimir/search/algorithms/iw/tuple_index_mapper.hpp"
#include "mimir/search/applicable_action_generators.hpp"
#include "mimir/search/axiom_evaluators.hpp"
#include "mimir/search/grounders.hpp"
#include "mimir/search/landmarks/fact_landmark_generator.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state.hpp"
#include "mimir/search/state_repository.hpp"

#include <algorithm>
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

size_t num_fluent_atoms(const Fixture& fixture)
{
    return boost::hana::at_key(fixture.problem->get_repositories().get_hana_repositories(), boost::hana::type<GroundAtomImpl<FluentTag>> {}).size();
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

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableDenseLayoutsAgree)
{
    /* The two dense layouts differ only in where a cell lives, so they must be indistinguishable
       through the table's interface -- on every query, at every arity, and across the resizes that
       renumber free tuple indices under them. They are worth comparing precisely because they reach
       their answers by different routes: `TUPLE_MAJOR` tests a whole rank set with word masks where
       `RANK_MAJOR` probes one bit per pair, and only the former caches `L(s)` as a bitmap. Both are
       pinned explicitly, so neither side of the comparison can drift onto the other's layout if the
       default changes. */
    {
        for (const size_t arity : { size_t(1), size_t(2) })
        {
            auto fixture = Fixture {};
            const auto landmark_atoms = landmark_atom_indices(fixture);
            ASSERT_FALSE(landmark_atoms.empty());

            auto baseline_options = iw::LandmarkNoveltyTableOptions {};
            baseline_options.force_dense = true;
            baseline_options.dense_layout = iw::LandmarkDenseLayout::RANK_MAJOR;
            auto candidate_options = baseline_options;
            candidate_options.dense_layout = iw::LandmarkDenseLayout::TUPLE_MAJOR;

            // Starting at zero atoms forces the resize path, which each layout remaps its own way.
            auto baseline = iw::LandmarkNoveltyTable(landmark_atoms, arity, 0, baseline_options);
            auto candidate = iw::LandmarkNoveltyTable(landmark_atoms, arity, 0, candidate_options);
            ASSERT_TRUE(baseline.is_dense());
            ASSERT_TRUE(candidate.is_dense());

            const auto transitions = collect_transitions(fixture, 400);
            ASSERT_FALSE(transitions.empty());

            EXPECT_EQ(baseline.test_novelty_and_update_table(transitions.front().state), candidate.test_novelty_and_update_table(transitions.front().state));

            auto add_atom_indices = iw::AtomIndexList {};
            auto del_atom_indices = iw::AtomIndexList {};
            auto baseline_witnesses = iw::AtomIndexList {};
            auto candidate_witnesses = iw::AtomIndexList {};
            auto num_novel_read_only = size_t(0);

            for (const auto& transition : transitions)
            {
                const auto& fluent_atoms = transition.state.get_atoms<FluentTag>();
                const auto& succ_fluent_atoms = transition.succ_state.get_atoms<FluentTag>();
                add_atom_indices.clear();
                del_atom_indices.clear();
                for (const auto atom_index : succ_fluent_atoms)
                {
                    if (!fluent_atoms.get(atom_index))
                    {
                        add_atom_indices.push_back(atom_index);
                    }
                }
                for (const auto atom_index : fluent_atoms)
                {
                    if (!succ_fluent_atoms.get(atom_index))
                    {
                        del_atom_indices.push_back(atom_index);
                    }
                }

                const auto baseline_read_only = baseline.test_novelty_read_only(transition.state, transition.succ_state);
                EXPECT_EQ(baseline_read_only, candidate.test_novelty_read_only(transition.state, transition.succ_state)) << "arity " << arity;
                num_novel_read_only += baseline_read_only;

                EXPECT_EQ(baseline.test_novelty_read_only_from_delta(transition.state, add_atom_indices, del_atom_indices),
                          candidate.test_novelty_read_only_from_delta(transition.state, add_atom_indices, del_atom_indices))
                    << "arity " << arity;

                if (arity == 1)
                {
                    baseline.compute_transition_novel_fluent_atom_indices_read_only(transition.state, transition.succ_state, baseline_witnesses);
                    candidate.compute_transition_novel_fluent_atom_indices_read_only(transition.state, transition.succ_state, candidate_witnesses);
                    EXPECT_EQ(baseline_witnesses, candidate_witnesses);
                }

                EXPECT_EQ(baseline.test_novelty_and_update_table(transition.state, transition.succ_state),
                          candidate.test_novelty_and_update_table(transition.state, transition.succ_state))
                    << "arity " << arity;
            }

            // Without this the comparison could pass on a table that never marked anything.
            EXPECT_GT(num_novel_read_only, 0u) << "arity " << arity;
            EXPECT_EQ(baseline.get_table_size(), candidate.get_table_size()) << "cell counts are layout-independent";
        }
    }
}

TEST(MimirTests, SearchAlgorithmsIWDenseBitTablesAddressEveryCellIndependently)
{
    /* The bit arithmetic is tested here rather than only through a table driven by a fixture,
       because a fixture pins the geometry to whatever its problem happens to have. Both layouts
       pack a rank into a word, so an index that is right for fewer than 64 ranks can still be
       wrong past the first word boundary -- which is exactly the case a small problem never
       reaches. 130 ranks spans three words. */
    constexpr auto num_ranks = size_t(130);
    constexpr auto num_tuples = size_t(70);

    auto rank_major = iw::RankMajorBitTable(num_ranks, num_tuples);
    auto tuple_major = iw::TupleMajorBitTable(num_ranks, num_tuples);

    const auto should_be_set = [](size_t rank, size_t tuple_index) { return ((rank * 31 + tuple_index * 17) % 5) == 0; };

    for (size_t rank = 0; rank < num_ranks; ++rank)
    {
        for (size_t tuple_index = 0; tuple_index < num_tuples; ++tuple_index)
        {
            if (!should_be_set(rank, tuple_index))
            {
                continue;
            }
            const auto r = static_cast<uint32_t>(rank);
            const auto t = static_cast<iw::TupleIndex>(tuple_index);
            EXPECT_TRUE(rank_major.set(r, t)) << "first set of (" << rank << ", " << tuple_index << ") should report the cell as new";
            EXPECT_TRUE(tuple_major.set(r, t));
            EXPECT_FALSE(rank_major.set(r, t)) << "second set should report the cell as already marked";
            EXPECT_FALSE(tuple_major.set(r, t));
        }
    }

    // No cell may have collided with another: every one reads back exactly what was written.
    for (size_t rank = 0; rank < num_ranks; ++rank)
    {
        for (size_t tuple_index = 0; tuple_index < num_tuples; ++tuple_index)
        {
            const auto r = static_cast<uint32_t>(rank);
            const auto t = static_cast<iw::TupleIndex>(tuple_index);
            EXPECT_EQ(rank_major.get(r, t), should_be_set(rank, tuple_index)) << "rank-major (" << rank << ", " << tuple_index << ")";
            EXPECT_EQ(tuple_major.get(r, t), should_be_set(rank, tuple_index)) << "tuple-major (" << rank << ", " << tuple_index << ")";
        }
    }

    EXPECT_EQ(rank_major.get_num_cells(), num_ranks * num_tuples);
    EXPECT_EQ(tuple_major.get_num_cells(), num_ranks * num_tuples);
    // 130 ranks needs three words per row, so the tuple-major table pays for 192 ranks per tuple.
    EXPECT_EQ(tuple_major.get_row_words(), 3u);
    EXPECT_EQ(tuple_major.get_num_bytes(), num_tuples * 3u * 8u);
}

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableCarriesAFullyMarkedTableOutOfTupleMajor)
{
    /* The dense -> sparse switch has to carry the marks it already holds. Reaching that with a
       table built at zero atoms does not test much: the atom universe is exhausted by the first
       resize, so the switch happens while the table is still empty and there is nothing to lose.
       Building over the whole universe first, marking it, and only then growing past it puts real
       marks on the wrong side of the switch. */
    auto fixture = Fixture {};
    const auto landmark_atoms = landmark_atom_indices(fixture);
    ASSERT_FALSE(landmark_atoms.empty());
    const auto num_atoms = num_fluent_atoms(fixture);
    ASSERT_GT(num_atoms, 0u);

    const auto transitions = collect_transitions(fixture, 400);
    ASSERT_FALSE(transitions.empty());

    for (const auto layout : { iw::LandmarkDenseLayout::RANK_MAJOR, iw::LandmarkDenseLayout::TUPLE_MAJOR })
    {
        auto options = iw::LandmarkNoveltyTableOptions {};
        options.dense_layout = layout;

        /* Exactly what this layout needs over the real universe, so that the doubling a resize
           performs takes it over. `TUPLE_MAJOR` pads its rows, so it is charged more for the same
           cells and needs its own figure. */
        const auto num_ranks = landmark_atoms.size() + 1;
        const auto charged_ranks = (layout == iw::LandmarkDenseLayout::TUPLE_MAJOR) ? ((num_ranks + 63) / 64) * 64 : num_ranks;
        options.max_dense_table_bytes = (charged_ranks * (num_atoms + 1) + 7) / 8;
        /* This layout fits now and not after the doubling a resize performs. Asked of the pinned
           layout, not of `fits_dense`: a table keeps the layout it was built with, so a budget that
           `TUPLE_MAJOR` outgrows takes it to sparse even where `RANK_MAJOR` would still have fit. */
        ASSERT_EQ(iw::LandmarkNoveltyTable::select_dense_layout(num_ranks, num_atoms, 1, options), layout);
        ASSERT_NE(iw::LandmarkNoveltyTable::select_dense_layout(num_ranks, 2 * num_atoms, 1, options), layout);

        auto table = iw::LandmarkNoveltyTable(landmark_atoms, 1, num_atoms, options);
        ASSERT_TRUE(table.is_dense());

        for (const auto& transition : transitions)
        {
            table.test_novelty_and_update_table(transition.state, transition.succ_state);
        }
        ASSERT_TRUE(table.is_dense()) << "the sweep should have marked a dense table, not switched during it";

        /* Grow the universe past what the table was built for. A read-only delta query resizes for
           the atoms it is told become true, which is the one entry point that can widen the table
           without also marking anything -- so whatever survives afterwards came through the remap. */
        const auto beyond = iw::AtomIndexList { static_cast<iw::AtomIndex>(num_atoms + 1) };
        table.test_novelty_read_only_from_delta(transitions.front().state, beyond, iw::AtomIndexList {});
        ASSERT_FALSE(table.is_dense()) << "the grown table should have outgrown the budget";

        for (const auto& transition : transitions)
        {
            EXPECT_FALSE(table.test_novelty_read_only(transition.state, transition.succ_state))
                << "a mark was lost in the switch out of the dense layout";
        }
    }
}

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableDeltaQuerySurvivesTheSwitchOutOfTupleMajor)
{
    /* `TUPLE_MAJOR` answers the delta query from a cached `L(s)` bitmap, while every other layout
       answers it from a cached `L(s)` atom list. The delta query resizes for the added atoms after
       that cache is filled, and a resize can take the table over budget and switch it to sparse --
       so the form the cache was filled in is not the form the query ends up reading. Driving the
       query across a budget that is outgrown mid-sweep is what exercises that seam. */
    auto fixture = Fixture {};
    const auto landmark_atoms = landmark_atom_indices(fixture);
    ASSERT_FALSE(landmark_atoms.empty());

    const auto transitions = collect_transitions(fixture, 400);
    ASSERT_FALSE(transitions.empty());

    /* Exactly where the switch lands depends on the budget, and only a switch that happens inside
       the delta query's own resize -- between the cache being filled and being read -- exercises
       the seam. Sweeping the budget puts it there for some size rather than hoping one guess does. */
    auto num_switched = size_t(0);
    for (const size_t budget : { size_t(64), size_t(128), size_t(256), size_t(512), size_t(1024), size_t(4096), size_t(16384) })
    {
        for (const size_t arity : { size_t(1), size_t(2) })
        {
            auto switching_options = iw::LandmarkNoveltyTableOptions {};
            switching_options.dense_layout = iw::LandmarkDenseLayout::TUPLE_MAJOR;
            switching_options.max_dense_table_bytes = budget;
            auto sparse_options = iw::LandmarkNoveltyTableOptions {};
            sparse_options.max_dense_table_bytes = 0;

            auto switching = iw::LandmarkNoveltyTable(landmark_atoms, arity, 0, switching_options);
            auto reference = iw::LandmarkNoveltyTable(landmark_atoms, arity, 0, sparse_options);

            auto add_atom_indices = iw::AtomIndexList {};
            auto del_atom_indices = iw::AtomIndexList {};

            for (const auto& transition : transitions)
            {
                const auto& fluent_atoms = transition.state.get_atoms<FluentTag>();
                const auto& succ_fluent_atoms = transition.succ_state.get_atoms<FluentTag>();
                add_atom_indices.clear();
                del_atom_indices.clear();
                for (const auto atom_index : succ_fluent_atoms)
                {
                    if (!fluent_atoms.get(atom_index))
                    {
                        add_atom_indices.push_back(atom_index);
                    }
                }
                for (const auto atom_index : fluent_atoms)
                {
                    if (!succ_fluent_atoms.get(atom_index))
                    {
                        del_atom_indices.push_back(atom_index);
                    }
                }

                EXPECT_EQ(switching.test_novelty_read_only_from_delta(transition.state, add_atom_indices, del_atom_indices),
                          reference.test_novelty_read_only_from_delta(transition.state, add_atom_indices, del_atom_indices))
                    << "budget " << budget << ", arity " << arity;

                EXPECT_EQ(switching.test_novelty_and_update_table(transition.state, transition.succ_state),
                          reference.test_novelty_and_update_table(transition.state, transition.succ_state))
                    << "budget " << budget << ", arity " << arity;
            }

            num_switched += !switching.is_dense();
        }
    }

    EXPECT_GT(num_switched, 0u) << "no budget in the sweep was outgrown, so nothing switched";
}

TEST(MimirTests, SearchAlgorithmsLandmarkNoveltyTableTupleMajorIsChargedForItsPadding)
{
    /* `TUPLE_MAJOR` pads each free tuple's rank bitmap to a whole word, so a table over few
       landmarks costs materially more than one bit per cell. The budget has to see that, or a
       table admitted as "51 bytes" would allocate 808. */
    auto options = iw::LandmarkNoveltyTableOptions {};
    options.dense_layout = iw::LandmarkDenseLayout::TUPLE_MAJOR;

    // 101 tuples x 1 word per row = 808 bytes, against 51 for the unpadded layouts.
    options.max_dense_table_bytes = 808;
    EXPECT_EQ(iw::LandmarkNoveltyTable::select_dense_layout(4, 100, 1, options), iw::LandmarkDenseLayout::TUPLE_MAJOR);

    /* One byte short of the padded size, the padding is a reason to pick a cheaper dense layout --
       not a reason to give up on dense storage, which at 51 bytes is comfortably affordable. */
    options.max_dense_table_bytes = 807;
    EXPECT_EQ(iw::LandmarkNoveltyTable::select_dense_layout(4, 100, 1, options), iw::LandmarkDenseLayout::RANK_MAJOR);
    EXPECT_TRUE(iw::LandmarkNoveltyTable::fits_dense(4, 100, 1, options));

    // Below the unpadded size too, nothing dense fits and the table is sparse.
    options.max_dense_table_bytes = 50;
    EXPECT_EQ(iw::LandmarkNoveltyTable::select_dense_layout(4, 100, 1, options), std::nullopt);

    // The fallback is real: the built table uses the layout the selection promised, at its size.
    options.max_dense_table_bytes = 807;
    const auto fell_back = iw::LandmarkNoveltyTable(iw::AtomIndexList { 0, 1, 2 }, 1, 100, options);
    EXPECT_EQ(fell_back.get_dense_layout(), iw::LandmarkDenseLayout::RANK_MAJOR);
    EXPECT_EQ(fell_back.get_table_bytes(), 56u) << "4 x 101 cells packed into whole words";

    options.max_dense_table_bytes = 808;
    const auto table = iw::LandmarkNoveltyTable(iw::AtomIndexList { 0, 1, 2 }, 1, 100, options);
    EXPECT_EQ(table.get_dense_layout(), iw::LandmarkDenseLayout::TUPLE_MAJOR);
    EXPECT_EQ(table.get_table_bytes(), 808u);
    EXPECT_EQ(table.get_table_size(), 4u * 101u);
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

/// At arity 1 the landmark table skips the tuple generators altogether and writes atom indices
/// straight into its scratch tuples. That is only sound because `to_tuple_index({a}) == a` and the
/// all-placeholder tuple is `num_atoms` -- properties of `TupleIndexMapper` and the generators, not
/// of the table. Pinned here, because a generator that stopped agreeing with the closed form would
/// make the fast paths silently wrong rather than merely slow, and nothing else would notice.
TEST(MimirTests, SearchAlgorithmsIWArityOneTupleGeneratorsMatchTheClosedForm)
{
    auto fixture = Fixture {};
    const auto num_atoms = boost::hana::at_key(fixture.problem->get_repositories().get_hana_repositories(),
                                               boost::hana::type<GroundAtomImpl<FluentTag>> {})
                               .size();
    auto mapper = iw::TupleIndexMapper(1, num_atoms);
    auto state_generator = iw::StateTupleIndexGenerator(&mapper);
    auto pair_generator = iw::StatePairTupleIndexGenerator(&mapper);

    const auto transitions = collect_transitions(fixture, 200);
    ASSERT_FALSE(transitions.empty());

    auto num_nonempty_transition_halves = size_t(0);
    for (const auto& transition : transitions)
    {
        auto expected_state_tuples = std::vector<iw::TupleIndex> {};
        for (const auto atom_index : transition.state.get_atoms<FluentTag>())
        {
            expected_state_tuples.push_back(atom_index);
        }
        expected_state_tuples.push_back(num_atoms);

        auto actual_state_tuples = std::vector<iw::TupleIndex> {};
        for (auto it = state_generator.begin(transition.state); it != state_generator.end(); ++it)
        {
            actual_state_tuples.push_back(*it);
        }
        std::sort(actual_state_tuples.begin(), actual_state_tuples.end());
        std::sort(expected_state_tuples.begin(), expected_state_tuples.end());
        EXPECT_EQ(actual_state_tuples, expected_state_tuples);

        /* The transition half is the singletons of the added atoms -- and no empty tuple, which
           contains no added atom. */
        const auto& state_fluent_atoms = transition.state.get_atoms<FluentTag>();
        auto expected_transition_tuples = std::vector<iw::TupleIndex> {};
        for (const auto atom_index : transition.succ_state.get_atoms<FluentTag>())
        {
            if (!state_fluent_atoms.get(atom_index))
            {
                expected_transition_tuples.push_back(atom_index);
            }
        }

        auto actual_transition_tuples = std::vector<iw::TupleIndex> {};
        for (auto it = pair_generator.begin(transition.state, transition.succ_state); it != pair_generator.end(); ++it)
        {
            actual_transition_tuples.push_back(*it);
        }
        std::sort(actual_transition_tuples.begin(), actual_transition_tuples.end());
        std::sort(expected_transition_tuples.begin(), expected_transition_tuples.end());
        EXPECT_EQ(actual_transition_tuples, expected_transition_tuples);

        num_nonempty_transition_halves += expected_transition_tuples.empty() ? 0 : 1;
    }

    /* Without this the transition half could be vacuously equal on every transition. */
    EXPECT_GT(num_nonempty_transition_halves, 0u);
}

}
