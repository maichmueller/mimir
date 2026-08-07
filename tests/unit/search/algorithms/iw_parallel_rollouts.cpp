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

#include "mimir/search/algorithms/iw/parallel_rollouts.hpp"

#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/algorithms/iw.hpp"
#include "mimir/search/algorithms/strategies/layer_ordering_strategy.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <gtest/gtest.h>

#include <set>
#include <vector>

namespace mimir::tests
{

using namespace mimir::search;
using namespace mimir::formalism;

namespace
{

SearchContext create_grounded_context(const std::string& domain, const std::string& problem)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + domain);
    const auto problem_file = fs::path(std::string(DATA_DIR) + problem);
    return SearchContextImpl::create(ProblemImpl::create(domain_file, problem_file),
                                     SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
}

iw::ParallelRolloutOptions make_batch_options(size_t num_seeds, uint32_t num_threads)
{
    auto options = iw::ParallelRolloutOptions();
    options.num_threads = num_threads;
    options.options.max_arity = 1;
    for (size_t k = 0; k < num_seeds; ++k)
    {
        options.seeds.push_back(k);
    }
    return options;
}

}

/// K parallel rollouts must produce exactly the results of K serial rollouts with the same
/// seeds. This is the core correctness claim of the private-interning-table design.
TEST(MimirTests, SearchAlgorithmsIWParallelRolloutsMatchSerialTest)
{
    for (const auto& [domain, problem] : std::vector<std::pair<std::string, std::string>> { { "gripper/domain.pddl", "gripper/test_problem.pddl" },
                                                                                           { "blocks_4/domain.pddl", "blocks_4/test_problem.pddl" },
                                                                                           { "miconic/domain.pddl", "miconic/test_problem.pddl" } })
    {
        const auto context = create_grounded_context(domain, problem);
        const auto options = make_batch_options(8, 8);

        auto serial_options = options;
        serial_options.num_threads = 1;

        const auto serial = iw::find_rollouts_parallel(context, serial_options);
        const auto parallel = iw::find_rollouts_parallel(context, options);

        ASSERT_EQ(serial.size(), 8u) << domain;
        ASSERT_EQ(parallel.size(), 8u) << domain;

        for (size_t k = 0; k < serial.size(); ++k)
        {
            EXPECT_EQ(parallel[k].status, serial[k].status) << domain << " seed " << k;
            EXPECT_EQ(parallel[k].num_states, serial[k].num_states) << domain << " seed " << k;
            EXPECT_EQ(parallel[k].reached_fluent_atoms, serial[k].reached_fluent_atoms) << domain << " seed " << k;
            EXPECT_EQ(parallel[k].reached_derived_atoms, serial[k].reached_derived_atoms) << domain << " seed " << k;
        }
    }
}

/// IW(1)'s reached-atom set is order-dependent in general, but the instances above are small
/// enough to be insensitive, so every seed agrees there and a seed-plumbing or
/// cross-rollout-interference bug would slip through. This runs the same seed-for-seed
/// equivalence claim on rollouts that genuinely disagree.
///
/// The instance is load-bearing. This used to run on gripper, which is order-*insensitive*
/// (all 16 seeds reach the same atoms -- docs/PARALLEL_IW_ROLLOUTS_HANDOFF.md section 3.1),
/// so the only divergence was whatever the truncation manufactured: 1-2 distinct atom sets,
/// and the vacuity guard below failed outright whenever it collapsed to 1, which was ~20% of
/// runs. Blocksworld is order-sensitive for real. Measured over 40 freshly parsed `Problem`s
/// of the 11-block instance below, all 8 seeds reach 8 distinct atom sets every time.
///
/// The cap stays: it is the only coverage of `max_next_layer_states` under the randomized
/// layer ordering, and it keeps each rollout at ~60 states.
TEST(MimirTests, SearchAlgorithmsIWParallelRolloutsMatchSerialWhenRolloutsDivergeTest)
{
    const auto context = create_grounded_context("blocks_4/domain.pddl", "blocks_4/p09-easy.pddl");

    auto options = make_batch_options(8, 8);
    options.options.max_next_layer_states = 2;

    auto serial_options = options;
    serial_options.num_threads = 1;

    const auto serial = iw::find_rollouts_parallel(context, serial_options);
    const auto parallel = iw::find_rollouts_parallel(context, options);

    for (size_t k = 0; k < serial.size(); ++k)
    {
        EXPECT_EQ(parallel[k].status, serial[k].status) << "seed " << k;
        EXPECT_EQ(parallel[k].num_states, serial[k].num_states) << "seed " << k;
        EXPECT_EQ(parallel[k].reached_fluent_atoms, serial[k].reached_fluent_atoms) << "seed " << k;
    }

    // Guard the guard: if every seed still agreed, this test would prove nothing. Left at
    // "more than one" rather than the measured 8 so that a future change in traversal order
    // cannot turn a still-meaningful test red.
    auto distinct = std::set<std::vector<Index>> {};
    for (const auto& rollout : parallel)
    {
        auto atoms = std::vector<Index> {};
        for (const auto index : rollout.reached_fluent_atoms)
        {
            atoms.push_back(index);
        }
        distinct.insert(std::move(atoms));
    }
    EXPECT_GT(distinct.size(), 1u) << "seeds did not diverge, so this test is vacuous";
}

/// A rollout run through the batched API (private interning tables, re-created start state)
/// must reach exactly the atoms that the ordinary single-rollout path reaches for the same
/// seed. This is what guarantees the batched path did not silently change search semantics.
TEST(MimirTests, SearchAlgorithmsIWParallelRolloutsMatchSingleRolloutPathTest)
{
    const auto batch_context = create_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto batch = iw::find_rollouts_parallel(batch_context, make_batch_options(4, 4));

    for (size_t k = 0; k < batch.size(); ++k)
    {
        // A fresh context so the reference rollout also starts from an empty repository.
        const auto reference_context = create_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

        auto options = iw::Options();
        options.max_arity = 1;
        options.layer_ordering_strategy = RandomizedLayerOrderingStrategyImpl::create(k);
        options.randomize_equal_score_ties = true;
        options.equal_score_tie_seed = k;

        const auto result = iw::find_solution(reference_context, options);

        EXPECT_EQ(batch[k].status, result.status) << "seed " << k;
        EXPECT_EQ(batch[k].reached_fluent_atoms, reference_context->get_state_repository()->get_reached_fluent_ground_atoms_bitset()) << "seed " << k;
        EXPECT_EQ(batch[k].num_states, reference_context->get_state_repository()->get_state_count()) << "seed " << k;
    }
}

/// The `Problem`'s ground-atom repositories must not grow during a grounded batch. If this
/// ever regresses, the batch is racing on `loki::IndexedHashSet`, which has no locking.
TEST(MimirTests, SearchAlgorithmsIWParallelRolloutsLeaveRepositoriesFrozenTest)
{
    const auto context = create_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto& problem = *context->get_problem();

    const auto snapshot = [&]
    {
        auto sizes = std::vector<size_t> {};
        boost::hana::for_each(problem.get_repositories().get_hana_repositories(),
                              [&](auto&& pair) { sizes.push_back(boost::hana::second(pair).size()); });
        return sizes;
    };

    const auto before = snapshot();
    iw::find_rollouts_parallel(context, make_batch_options(8, 8));
    const auto after = snapshot();

    EXPECT_EQ(before, after);
}

/// Private interning tables mean a state repository must never observe another's slots. The
/// batch must therefore leave the shared `Problem`'s tables untouched.
TEST(MimirTests, SearchAlgorithmsIWParallelRolloutsDoNotTouchSharedInterningTablesTest)
{
    const auto context = create_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    auto& problem = *context->get_problem();

    const auto index_tree_before = problem.get_index_tree_table().size();
    const auto double_leaf_before = problem.get_double_leaf_table().size();

    iw::find_rollouts_parallel(context, make_batch_options(8, 8));

    EXPECT_EQ(problem.get_index_tree_table().size(), index_tree_before);
    EXPECT_EQ(problem.get_double_leaf_table().size(), double_leaf_before);
}

/// Lifted contexts must be rejected rather than silently racing on `Repositories`.
TEST(MimirTests, SearchAlgorithmsIWParallelRolloutsRejectLiftedContextTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "gripper/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl");
    const auto context = SearchContextImpl::create(ProblemImpl::create(domain_file, problem_file),
                                                   SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));

    EXPECT_THROW(iw::find_rollouts_parallel(context, make_batch_options(4, 4)), std::runtime_error);
}

/// A beam width cannot work with the randomized layer ordering that makes rollouts differ.
/// It must be rejected on the calling thread: if it reached the workers it would throw
/// inside the pool, and the pool is built with exception handling disabled, so the process
/// would abort instead of reporting an error.
TEST(MimirTests, SearchAlgorithmsIWParallelRolloutsRejectBeamWidthTest)
{
    const auto context = create_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    auto options = make_batch_options(4, 4);
    options.options.beam_width = 8;

    EXPECT_THROW(iw::find_rollouts_parallel(context, options), std::runtime_error);

    // Multi-threaded too, since that is the path that would have aborted.
    auto parallel_options = make_batch_options(8, 8);
    parallel_options.options.beam_width = 8;
    EXPECT_THROW(iw::find_rollouts_parallel(context, parallel_options), std::runtime_error);
}

/// An empty seed list is a no-op, not a crash.
TEST(MimirTests, SearchAlgorithmsIWParallelRolloutsEmptyBatchTest)
{
    const auto context = create_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");
    EXPECT_TRUE(iw::find_rollouts_parallel(context, make_batch_options(0, 4)).empty());
}

/// Co-occurrence capture is opt-in, like landing states: a default batch must not pay for it.
TEST(MimirTests, SearchAlgorithmsIWParallelRolloutsCoOccurrenceIsOptInTest)
{
    const auto context = create_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    for (const auto& rollout : iw::find_rollouts_parallel(context, make_batch_options(4, 4)))
    {
        EXPECT_TRUE(rollout.co_occurrence_by_atom.empty());
    }
    EXPECT_TRUE(iw::intersect_co_occurrence(iw::find_rollouts_parallel(context, make_batch_options(4, 4))).empty());
}

/// Accumulating during search must give exactly what unpacking every created state afterwards
/// would give. That equivalence is the whole justification for the tracker: the rows are
/// reconstructible, just not affordably, because reconstruction unpacks the entire repository.
///
/// Deliberately checked against the repository the search actually ran in, not against a replay
/// in a fresh one. A replay compares two searches, and under a randomized layer ordering those
/// need not explore the same states at all -- in blocksworld they reliably do not.
TEST(MimirTests, StateRepositoryCoOccurrenceMatchesUnpackedStatesTest)
{
    for (const auto& [domain, problem] : std::vector<std::pair<std::string, std::string>> { { "gripper/domain.pddl", "gripper/test_problem.pddl" },
                                                                                           { "blocks_4/domain.pddl", "blocks_4/p09-easy.pddl" } })
    {
        const auto context = create_grounded_context(domain, problem);
        auto& repository = *context->get_state_repository();
        repository.enable_co_occurrence_tracking();  ///< before any state exists

        auto options = iw::Options();
        options.max_arity = 2;
        iw::find_solution(context, options);

        const auto& rows = repository.get_co_occurrence_by_atom();

        auto expected = std::vector<FlatBitset>(rows.size());
        for (size_t index = 0; index < repository.get_state_count(); ++index)
        {
            const auto atoms = repository.get_state(*repository.get_packed_state(static_cast<Index>(index))).get_atoms<FluentTag>();
            for (const auto atom_index : atoms)
            {
                ASSERT_LT(atom_index, expected.size()) << domain << " atom " << atom_index;
                expected[atom_index] |= atoms;
            }
        }

        EXPECT_EQ(rows, expected) << domain;

        // Vacuity guards: all-empty rows, or a single state, would pass the above for free.
        EXPECT_GT(rows.size(), 0u) << domain;
        EXPECT_GT(repository.get_state_count(), 1u) << domain;
    }
}

/// Tracking must not perturb the search it observes.
///
/// Both contexts are built over ONE parsed `Problem` and one generator, differing only in their
/// repository. Two separately parsed problems would not be a control: measured on the instance
/// below, IW(2) over a freshly parsed problem reaches a different state count on every run even
/// with the default, non-randomized options -- 7188/7660/7430 across three runs -- so a
/// re-parsing comparison would attribute that spread to the tracker.
TEST(MimirTests, StateRepositoryCoOccurrenceDoesNotChangeSearchTest)
{
    const auto plain_context = create_grounded_context("blocks_4/domain.pddl", "blocks_4/p09-easy.pddl");

    const auto tracked_repository =
        StateRepositoryImpl::create(plain_context->get_state_repository()->get_axiom_evaluator(), StateRepositoryImpl::PrivateInterningTables {});
    tracked_repository->enable_co_occurrence_tracking();  ///< before any state exists
    const auto tracked_context =
        SearchContextImpl::create(plain_context->get_problem(), plain_context->get_applicable_action_generator(), tracked_repository);

    auto options = iw::Options();
    options.max_arity = 2;

    const auto plain = iw::find_solution(plain_context, options);
    const auto tracked = iw::find_solution(tracked_context, options);

    EXPECT_EQ(tracked.status, plain.status);
    EXPECT_EQ(tracked_repository->get_state_count(), plain_context->get_state_repository()->get_state_count());
    EXPECT_EQ(tracked_repository->get_reached_fluent_ground_atoms_bitset(),
              plain_context->get_state_repository()->get_reached_fluent_ground_atoms_bitset());
    EXPECT_FALSE(tracked_repository->get_co_occurrence_by_atom().empty());
}

/// Every atom the batch reached must have a row, and no row may claim an atom the rollout never
/// reached. This is what makes the rows safe to intersect against `reached_fluent_atoms`.
TEST(MimirTests, SearchAlgorithmsIWParallelRolloutsCoOccurrenceAgreesWithReachedAtomsTest)
{
    const auto context = create_grounded_context("blocks_4/domain.pddl", "blocks_4/p09-easy.pddl");
    auto options = make_batch_options(4, 4);
    options.options.max_arity = 2;
    options.report_co_occurrence = true;

    for (const auto& rollout : iw::find_rollouts_parallel(context, options))
    {
        auto covered = FlatBitset {};
        for (size_t atom_index = 0; atom_index < rollout.co_occurrence_by_atom.size(); ++atom_index)
        {
            const auto& row = rollout.co_occurrence_by_atom[atom_index];
            if (row.count() == 0)
            {
                continue;
            }
            covered.set(atom_index);
            // An atom co-occurs with itself, and only with atoms this rollout also reached.
            EXPECT_TRUE(row.get(atom_index)) << "atom " << atom_index << " missing from its own row";
            auto outside = row;
            outside -= rollout.reached_fluent_atoms;
            EXPECT_EQ(outside.count(), 0u) << "atom " << atom_index << " co-occurs with unreached atoms";
        }
        EXPECT_EQ(covered, rollout.reached_fluent_atoms) << "rows and reached atoms disagree";
    }
}

/// Parallel and serial batches must agree on the rows, exactly as they do on reached atoms.
TEST(MimirTests, SearchAlgorithmsIWParallelRolloutsCoOccurrenceMatchesSerialTest)
{
    const auto context = create_grounded_context("blocks_4/domain.pddl", "blocks_4/p09-easy.pddl");

    auto options = make_batch_options(8, 8);
    options.options.max_next_layer_states = 2;  ///< as in the divergence test above
    options.report_co_occurrence = true;

    auto serial_options = options;
    serial_options.num_threads = 1;

    const auto serial = iw::find_rollouts_parallel(context, serial_options);
    const auto parallel = iw::find_rollouts_parallel(context, options);

    for (size_t k = 0; k < serial.size(); ++k)
    {
        EXPECT_EQ(parallel[k].co_occurrence_by_atom, serial[k].co_occurrence_by_atom) << "seed " << k;
    }
}

/// The fold must require unanimity: every pair it keeps is a pair every rollout saw, and an
/// atom one rollout never reached contributes nothing rather than being skipped over.
TEST(MimirTests, SearchAlgorithmsIWParallelRolloutsIntersectCoOccurrenceTest)
{
    const auto context = create_grounded_context("blocks_4/domain.pddl", "blocks_4/p09-easy.pddl");

    auto options = make_batch_options(8, 8);
    options.options.max_next_layer_states = 2;  ///< forces the seeds apart; see above
    options.report_co_occurrence = true;

    const auto batch = iw::find_rollouts_parallel(context, options);
    const auto intersected = iw::intersect_co_occurrence(batch);

    ASSERT_EQ(intersected.size(), batch.front().co_occurrence_by_atom.size());

    auto intersected_bits = size_t(0);
    auto union_bits = size_t(0);
    for (size_t atom_index = 0; atom_index < intersected.size(); ++atom_index)
    {
        auto expected = batch.front().co_occurrence_by_atom[atom_index];
        for (const auto& rollout : batch)
        {
            if (atom_index >= rollout.co_occurrence_by_atom.size())
            {
                expected.unset_all();
                break;
            }
            expected &= rollout.co_occurrence_by_atom[atom_index];
        }
        EXPECT_EQ(intersected[atom_index], expected) << "atom " << atom_index;

        intersected_bits += intersected[atom_index].count();
        for (const auto& rollout : batch)
        {
            if (atom_index < rollout.co_occurrence_by_atom.size())
            {
                auto merged = rollout.co_occurrence_by_atom[atom_index];
                merged |= intersected[atom_index];
                union_bits += merged.count() - intersected[atom_index].count();
            }
        }
    }

    // Guard the guard: with all seeds agreeing the fold would be a no-op and prove nothing.
    EXPECT_GT(union_bits, 0u) << "seeds did not diverge, so this test is vacuous";
    EXPECT_GT(intersected_bits, 0u) << "the fold emptied everything, so it is not measuring a fold";
}

/// A repository with private interning tables must behave exactly like the default one when
/// used on its own -- same states, same reached atoms.
TEST(MimirTests, StateRepositoryPrivateInterningTablesMatchSharedTest)
{
    const auto shared_context = create_grounded_context("gripper/domain.pddl", "gripper/test_problem.pddl");

    const auto private_repository =
        StateRepositoryImpl::create(shared_context->get_state_repository()->get_axiom_evaluator(), StateRepositoryImpl::PrivateInterningTables {});
    const auto private_context =
        SearchContextImpl::create(shared_context->get_problem(), shared_context->get_applicable_action_generator(), private_repository);

    auto options = iw::Options();
    options.max_arity = 1;

    const auto shared_result = iw::find_solution(shared_context, options);
    const auto private_result = iw::find_solution(private_context, options);

    EXPECT_EQ(private_result.status, shared_result.status);
    EXPECT_EQ(private_repository->get_state_count(), shared_context->get_state_repository()->get_state_count());
    EXPECT_EQ(private_repository->get_reached_fluent_ground_atoms_bitset(),
              shared_context->get_state_repository()->get_reached_fluent_ground_atoms_bitset());
}

}
