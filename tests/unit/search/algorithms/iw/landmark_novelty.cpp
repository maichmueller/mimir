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

#include "mimir/common/filesystem.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/algorithms/iw.hpp"
#include "mimir/search/algorithms/iw/event_handlers.hpp"
#include "mimir/search/applicable_action_generators.hpp"
#include "mimir/search/axiom_evaluators.hpp"
#include "mimir/search/grounders.hpp"
#include "mimir/search/landmarks/fact_landmark_generator.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"
#include "mimir/search/plan.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace mimir::formalism;
using namespace mimir::search;
using namespace mimir::search::landmarks;

namespace mimir::tests
{
namespace
{

/// @brief A grounded instance plus its landmark graphs, reused across configurations.
///
/// Reuse is not just an optimization. Two `Problem` instances of the same PDDL can enumerate
/// grounded actions in different orders (match-tree split ties are broken by pointer-set order),
/// and under novelty pruning generation order decides which of several candidates claims a
/// contested tuple. Expansion counts therefore only compare meaningfully when both configurations
/// run against the *same* grounded instance. Running two searches over one context is safe: novelty
/// tables live in the pruning strategy and `is_new_successor` is read off the per-search node
/// vector, not off the shared state repository.
struct Instance
{
    Problem problem;
    LiftedGrounder grounder;
    GroundedAxiomEvaluator axiom_evaluator;
    StateRepository state_repository;
    GroundedApplicableActionGenerator action_generator;
    SearchContext context;
    FactLandmarkGraph landmarks;
    FactLandmarkGraph empty_landmarks;
    /// The same graph with disjunctive landmarks computed; its fact landmarks are identical.
    FactLandmarkGraph disjunctive_landmarks;

    Instance(const std::string& domain, const std::string& instance) :
        problem(ProblemImpl::create(fs::path(std::string(DATA_DIR) + domain + "/domain.pddl"), fs::path(std::string(DATA_DIR) + domain + "/" + instance))),
        grounder(problem),
        axiom_evaluator(grounder.create_grounded_axiom_evaluator()),
        state_repository(StateRepositoryImpl::create(axiom_evaluator)),
        action_generator(grounder.create_grounded_applicable_action_generator()),
        context(SearchContextImpl::create(problem, action_generator, state_repository)),
        landmarks(ApproximateFactLandmarkGenerator::create(grounder)),
        empty_landmarks(nullptr),
        disjunctive_landmarks(nullptr)
    {
        // Goal seeding is the only seed source, so switching it off yields an empty landmark set.
        auto options = FactLandmarkGeneratorOptions {};
        options.include_positive_goal_facts = false;
        empty_landmarks = ApproximateFactLandmarkGenerator::create(grounder, options);

        auto disjunctive_options = FactLandmarkGeneratorOptions {};
        disjunctive_options.max_disjunctive_landmark_size = 4;
        disjunctive_landmarks = ApproximateFactLandmarkGenerator::create(grounder, disjunctive_options);
    }

    size_t get_num_fluent_atoms() const
    {
        return boost::hana::at_key(problem->get_repositories().get_hana_repositories(), boost::hana::type<GroundAtomImpl<FluentTag>> {}).size();
    }
};

struct RunResult
{
    SearchStatus status;
    size_t plan_length;
    uint64_t num_expanded;
};

enum class Novelty
{
    PLAIN,
    /// Landmark-restricted novelty over the goal-seeded approximate fact landmarks.
    LANDMARK,
    /// Landmark-restricted novelty over an intentionally *empty* landmark set, which must make the
    /// feature family collapse back onto plain IW(k).
    LANDMARK_WITHOUT_LANDMARKS,
};

RunResult run_iw(const Instance& instance, size_t max_arity, Novelty novelty, bool disjunctive = false, const IndexSet& unshared_atoms = {}, bool all_private = false)
{
    auto event_handler = iw::DefaultEventHandlerImpl::create(instance.problem, true);

    auto options = iw::Options {};
    options.max_arity = max_arity;
    options.iw_event_handler = event_handler;
    options.max_num_states = 500000;
    options.landmark_novelty_disjunctive = disjunctive;
    options.landmark_novelty_unshared_atoms = unshared_atoms;
    options.landmark_novelty_all_private = all_private;
    switch (novelty)
    {
        case Novelty::PLAIN:
            break;
        case Novelty::LANDMARK:
            options.landmark_novelty_graph = disjunctive ? instance.disjunctive_landmarks : instance.landmarks;
            break;
        case Novelty::LANDMARK_WITHOUT_LANDMARKS:
            options.landmark_novelty_graph = instance.empty_landmarks;
            break;
    }

    const auto result = iw::find_solution(instance.context, options);

    auto num_expanded = uint64_t(0);
    for (const auto& statistics : event_handler->get_statistics().get_brfs_statistics_by_arity())
    {
        num_expanded += statistics.get_num_expanded();
    }

    return RunResult { result.status, result.plan.has_value() ? result.plan->get_length() : 0, num_expanded };
}

/// @brief Instances on which plain IW(1) is not wide enough but LIW(1) is. This is the whole point
/// of the variant, so it is asserted across a spread of domains rather than a single fixture.
const std::vector<std::pair<std::string, std::string>>& width_gap_instances()
{
    static const auto instances = std::vector<std::pair<std::string, std::string>> {
        { "blocks_3", "test_problem.pddl" },   { "blocks_4", "test_problem.pddl" }, { "gripper", "test_problem.pddl" },   { "spanner", "test_problem.pddl" },
        { "childsnack", "test_problem.pddl" }, { "delivery", "test_problem.pddl" }, { "logistics", "test_problem.pddl" }, { "visitall", "test_problem.pddl" },
        { "satellite", "test_problem.pddl" },  { "rovers", "test_problem.pddl" },   { "transport", "test_problem.pddl" }
    };
    return instances;
}

}

TEST(MimirTests, SearchAlgorithmsIWLandmarkNoveltyClosesTheWidthOneGap)
{
    // LIW(1) admits everything IW(1) admits and more, so on a problem of width 2 it can reach a
    // goal that IW(1) prunes away -- without paying for a full IW(2) pass.
    for (const auto& [domain, instance_file] : width_gap_instances())
    {
        auto instance = Instance(domain, instance_file);
        ASSERT_GT(instance.landmarks->get_landmark_atom_indices().size(), 0u) << domain;

        const auto plain = run_iw(instance, 1, Novelty::PLAIN);
        const auto landmark = run_iw(instance, 1, Novelty::LANDMARK);

        EXPECT_EQ(plain.status, SearchStatus::FAILED) << domain << ": expected IW(1) to be too narrow here";
        EXPECT_EQ(landmark.status, SearchStatus::SOLVED) << domain << ": expected LIW(1) to close the gap";
        EXPECT_GT(landmark.plan_length, 0u) << domain;
    }
}

TEST(MimirTests, SearchAlgorithmsIWLandmarkNoveltyRespectsItsExpansionBound)
{
    // Every expanded state must claim a previously unseen feature, and there are at most
    // (|L| + 1) * (N + 1) landmark-restricted features at arity 1. Blowing past that means the
    // table is failing to record what it hands out.
    for (const auto& [domain, instance_file] : width_gap_instances())
    {
        auto instance = Instance(domain, instance_file);
        const auto landmark = run_iw(instance, 1, Novelty::LANDMARK);

        const auto num_landmarks = instance.landmarks->get_landmark_atom_indices().size();
        const auto bound = (num_landmarks + 1) * (instance.get_num_fluent_atoms() + 1);
        EXPECT_LE(landmark.num_expanded, bound) << domain << ": expanded " << landmark.num_expanded << " with |L|=" << num_landmarks
                                                << " and N=" << instance.get_num_fluent_atoms();
    }
}

TEST(MimirTests, SearchAlgorithmsIWLandmarkNoveltyWithoutLandmarksMatchesPlainIW)
{
    /* With no landmarks at all every state falls back to the BOT coordinate, so the search must
       reproduce plain IW expansion for expansion.

       `max_arity` is 2 rather than 1 on purpose: at `max_arity == 1` plain IW enables
       `optimize_iw1_root_actions`, which skips the standalone arity-0 pass and hands the arity-1
       pass extra root bookkeeping that only `ArityKNoveltyPruningStrategyImpl` implements. That
       makes the two configurations different *searches*, not different feature families, and the
       counts legitimately diverge. From `max_arity == 2` on, the optimization is off for both. */
    for (const auto& [domain, instance_file] : width_gap_instances())
    {
        auto instance = Instance(domain, instance_file);
        ASSERT_EQ(instance.empty_landmarks->get_landmark_atom_indices().size(), 0u) << domain;

        const auto plain = run_iw(instance, 2, Novelty::PLAIN);
        const auto degenerate = run_iw(instance, 2, Novelty::LANDMARK_WITHOUT_LANDMARKS);

        EXPECT_EQ(degenerate.status, plain.status) << domain;
        EXPECT_EQ(degenerate.plan_length, plain.plan_length) << domain;
        EXPECT_EQ(degenerate.num_expanded, plain.num_expanded) << domain;
    }
}

TEST(MimirTests, SearchAlgorithmsIWLandmarkNoveltyRejectsAtomLevelAccelerators)
{
    auto instance = Instance("blocks_3", "test_problem.pddl");

    auto options = iw::Options {};
    options.max_arity = 1;
    options.iw_event_handler = iw::DefaultEventHandlerImpl::create(instance.problem, true);
    options.landmark_novelty_graph = instance.landmarks;

    /* These two do not survive the move from atom-level features to (landmark, free tuple) pairs,
       for two different reasons:

       - atom-first mode filters actions by `test_atom_novelty_read_only`, which is handed an atom
         index with no state and no transition. There is nothing to quantify the landmark
         coordinate over, so no exact answer exists even in principle.
       - incremental first-applicability tests every ground action at most once across the whole
         search. That is sound under IW(1), where every atom of every generated state is marked, so
         a re-application can never add an unmarked atom. Under LIW it is not: the same action at a
         state with different coordinates can expose a pair nothing has marked.

       The add-effect precheck is deliberately NOT in this list -- it gets the whole transition and
       is exact; see `landmark_novelty_precheck.cpp`. */
    for (const auto set_option :
         std::vector<void (*)(iw::Options&)> {
             [](iw::Options& o) { o.iw1_atom_first_mode = true; },
             [](iw::Options& o) { o.iw1_incremental_first_applicability = true; },
             [](iw::Options& o) { o.iw1_incremental_first_applicability_debug_crosscheck = true; },
         })
    {
        auto bad_options = options;
        set_option(bad_options);
        EXPECT_THROW(iw::find_solution(instance.context, bad_options), std::invalid_argument);
    }
}

TEST(MimirTests, SearchAlgorithmsIWLandmarkNoveltyAcceptsTheAddEffectPrecheck)
{
    auto instance = Instance("blocks_3", "test_problem.pddl");

    auto options = iw::Options {};
    options.max_arity = 1;
    options.iw_event_handler = iw::DefaultEventHandlerImpl::create(instance.problem, true);
    options.landmark_novelty_graph = instance.landmarks;
    options.iw1_precheck_add_effect_novelty = true;

    EXPECT_NO_THROW(iw::find_solution(instance.context, options));
}

TEST(MimirTests, SearchAlgorithmsIWDisjunctiveNoveltyIsOffByDefault)
{
    /* The graph carries disjunctive landmarks and the search ignores them unless asked. Without
       this, turning them on in the generator would silently change every LIW search. */
    for (const auto& [domain, instance_file] : width_gap_instances())
    {
        auto instance = Instance(domain, instance_file);
        ASSERT_EQ(instance.landmarks->get_landmark_atom_indices(), instance.disjunctive_landmarks->get_landmark_atom_indices()) << domain;

        const auto without = run_iw(instance, 1, Novelty::LANDMARK);
        const auto with_graph_flag_off = run_iw(instance, 1, Novelty::LANDMARK, false);

        EXPECT_EQ(with_graph_flag_off.status, without.status) << domain;
        EXPECT_EQ(with_graph_flag_off.plan_length, without.plan_length) << domain;
        EXPECT_EQ(with_graph_flag_off.num_expanded, without.num_expanded) << domain;
    }
}

TEST(MimirTests, SearchAlgorithmsIWDisjunctiveNoveltyStillSolvesTheWidthOneGap)
{
    /* Ranking the disjunct members adds coordinates and shares rows among them at the same time,
       so the expansion count can move either way. What must not move is the answer. */
    for (const auto& [domain, instance_file] : width_gap_instances())
    {
        auto instance = Instance(domain, instance_file);
        const auto disjunctive = run_iw(instance, 1, Novelty::LANDMARK, true);

        EXPECT_EQ(disjunctive.status, SearchStatus::SOLVED) << domain;
        EXPECT_GT(disjunctive.plan_length, 0u) << domain;
    }
}

TEST(MimirTests, SearchAlgorithmsIWDisjunctiveNoveltyWithoutDisjunctsIsANoOp)
{
    /* A graph built without `max_disjunctive_landmark_size` has nothing to group, so asking for
       disjunctive novelty over it must reproduce the plain landmark search exactly. */
    for (const auto& [domain, instance_file] : width_gap_instances())
    {
        auto instance = Instance(domain, instance_file);
        auto event_handler = iw::DefaultEventHandlerImpl::create(instance.problem, true);
        auto options = iw::Options {};
        options.max_arity = 1;
        options.iw_event_handler = event_handler;
        options.max_num_states = 500000;
        options.landmark_novelty_graph = instance.landmarks;  // no disjunctive landmarks on it
        options.landmark_novelty_disjunctive = true;
        const auto result = iw::find_solution(instance.context, options);

        auto num_expanded = uint64_t(0);
        for (const auto& statistics : event_handler->get_statistics().get_brfs_statistics_by_arity())
        {
            num_expanded += statistics.get_num_expanded();
        }

        const auto baseline = run_iw(instance, 1, Novelty::LANDMARK);
        EXPECT_EQ(result.status, baseline.status) << domain;
        EXPECT_EQ(num_expanded, baseline.num_expanded) << domain;
    }
}

TEST(MimirTests, SearchAlgorithmsIWUnsharedAtomsWeakenPruning)
{
    /* Un-sharing gives an atom a private row, so it can no longer be pruned on a sibling's marks:
       the search admits at least as much as it did while sharing. Exempting *every* member makes
       the grouping trivial again, which is the extreme case worth pinning -- it must then match the
       plain landmark search exactly. */
    for (const auto& [domain, instance_file] : width_gap_instances())
    {
        auto instance = Instance(domain, instance_file);
        const auto& disjunctive = instance.disjunctive_landmarks->get_disjunctive_landmarks();
        if (disjunctive.empty())
        {
            continue;  // nothing shared here, so nothing to exempt
        }

        auto every_member = IndexSet {};
        for (const auto& members : disjunctive)
        {
            every_member.insert(members.begin(), members.end());
        }

        const auto shared = run_iw(instance, 1, Novelty::LANDMARK, true);
        const auto fully_unshared = run_iw(instance, 1, Novelty::LANDMARK, true, every_member);

        EXPECT_GE(fully_unshared.num_expanded, shared.num_expanded) << domain;
        EXPECT_EQ(fully_unshared.status, shared.status) << domain;
    }
}

TEST(MimirTests, SearchAlgorithmsIWAllPrivateMatchesExplicitUnsharedUnion)
{
    for (const auto& [domain, instance_file] : width_gap_instances())
    {
        auto instance = Instance(domain, instance_file);
        const auto& disjunctive = instance.disjunctive_landmarks->get_disjunctive_landmarks();
        if (disjunctive.empty())
        {
            continue;
        }

        auto every_member = IndexSet {};
        for (const auto& members : disjunctive)
        {
            every_member.insert(members.begin(), members.end());
        }

        const auto explicit_unshared = run_iw(instance, 1, Novelty::LANDMARK, true, every_member);
        const auto all_private = run_iw(instance, 1, Novelty::LANDMARK, true, {}, true);

        EXPECT_EQ(all_private.status, explicit_unshared.status) << domain;
        EXPECT_EQ(all_private.plan_length, explicit_unshared.plan_length) << domain;
        EXPECT_EQ(all_private.num_expanded, explicit_unshared.num_expanded) << domain;
    }
}

}
