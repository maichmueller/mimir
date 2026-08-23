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

/// Gate for T2 of docs/ROLLOUT_IW_IMPLEMENTATION_PLAN.md: `ProblemImpl::create_grounding_overlay`.
/// The invariant every test here defends is that a worker searching through an overlay can ground
/// as much as it likes without any of it becoming visible in the parent -- that is what makes K
/// lifted searches over one parsed model safe, and it is not enforced by any lock.

#include "mimir/formalism/problem.hpp"

#include "mimir/formalism/domain.hpp"
#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/algorithms/brfs.hpp"
#include "mimir/search/applicable_action_generators/lifted/kpkc.hpp"
#include "mimir/search/axiom_evaluators/lifted/kpkc.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace mimir::tests
{

using namespace mimir::formalism;
using namespace mimir::search;

namespace
{

Problem parse_problem(const std::string& domain, const std::string& problem)
{
    return ProblemImpl::create(fs::path(std::string(DATA_DIR) + domain), fs::path(std::string(DATA_DIR) + problem));
}

/// Per-repository element counts, in the fixed hana order. Comparing two of these is the cheapest
/// way to assert "nothing was interned here", and it catches growth in any of the ~70 repositories
/// rather than only the ones a test thought to name.
std::vector<size_t> repository_sizes(const ProblemImpl& problem)
{
    auto sizes = std::vector<size_t> {};
    boost::hana::for_each(problem.get_repositories().get_hana_repositories(), [&](auto&& pair) { sizes.push_back(boost::hana::second(pair).size()); });
    return sizes;
}

/// Local element counts only -- what this level interned itself, excluding everything inherited.
std::vector<size_t> local_repository_sizes(const ProblemImpl& problem)
{
    auto sizes = std::vector<size_t> {};
    boost::hana::for_each(problem.get_repositories().get_hana_repositories(),
                          [&](auto&& pair) { sizes.push_back(boost::hana::second(pair).get_vector().size()); });
    return sizes;
}

/// The first action schema with at least one applicable ground instance, together with a binding
/// taken from the problem's own objects. Enough to make `ground(...)` do real work.
std::pair<Action, ObjectList> pick_groundable_action(const ProblemImpl& problem)
{
    for (const auto& action : problem.get_domain()->get_actions())
    {
        const auto arity = action->get_arity();
        if (arity > problem.get_problem_and_domain_objects().size())
        {
            continue;
        }

        auto binding = ObjectList {};
        for (size_t i = 0; i < arity; ++i)
        {
            binding.push_back(problem.get_problem_and_domain_objects().at(i));
        }
        return { action, binding };
    }
    throw std::runtime_error("pick_groundable_action: the test problem has no action schema.");
}

SearchContext create_overlay_context(const Problem& overlay)
{
    auto generator = std::make_shared<KPKCLiftedApplicableActionGeneratorImpl>(
        overlay,
        SearchContextImpl::LiftedOptions::KPKCOptions(SearchContextImpl::SymmetryPruning::OFF));
    auto state_repository = StateRepositoryImpl::create(std::make_shared<KPKCLiftedAxiomEvaluatorImpl>(overlay));
    return SearchContextImpl::create(overlay, std::move(generator), std::move(state_repository));
}

}

/// An overlay must start out looking exactly like its parent, without reparsing or pre-grounding.
TEST(MimirTests, MimirFormalismProblemGroundingOverlayInheritsDescriptionTest)
{
    const auto parent = parse_problem("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto overlay = ProblemImpl::create_grounding_overlay(parent);

    EXPECT_TRUE(overlay->is_grounding_overlay());
    EXPECT_EQ(overlay->get_overlay_parent(), parent);
    EXPECT_FALSE(parent->is_grounding_overlay());
    EXPECT_EQ(parent->get_overlay_parent(), nullptr);

    /* Shared by pointer, not rebuilt. */
    EXPECT_EQ(overlay->get_domain(), parent->get_domain());
    EXPECT_EQ(overlay->get_name(), parent->get_name());
    EXPECT_EQ(overlay->get_objects(), parent->get_objects());
    EXPECT_EQ(overlay->get_problem_and_domain_objects(), parent->get_problem_and_domain_objects());
    EXPECT_EQ(overlay->get_axioms(), parent->get_axioms());
    EXPECT_EQ(overlay->get_fluent_initial_atoms(), parent->get_fluent_initial_atoms());
    EXPECT_EQ(overlay->get_static_initial_atoms(), parent->get_static_initial_atoms());
    EXPECT_EQ(&overlay->get_static_assignment_sets(), &parent->get_static_assignment_sets());

    /* The goal condition is the parent's object, not an equivalent copy. A worker searching an
       overlay for the parent's goal must be testing the very same condition. */
    EXPECT_EQ(overlay->get_goal_condition(), parent->get_goal_condition());
    EXPECT_EQ(overlay->static_goal_holds(), parent->static_goal_holds());

    /* Every repository starts at the parent's size, and holds nothing of its own -- the overlay
       inherits the whole index space rather than copying it. A fresh overlay has interned nothing
       whatsoever, so its first local index is exactly the parent's current size. */
    EXPECT_EQ(repository_sizes(*overlay), repository_sizes(*parent));
    for (const auto local_size : local_repository_sizes(*overlay))
    {
        EXPECT_EQ(local_size, 0u);
    }

    /* Its own, empty grounding workspace. */
    EXPECT_EQ(overlay->get_index_tree_table().size(), 0u);
    EXPECT_EQ(overlay->get_double_leaf_table().size(), 0u);
}

/// Grounding through an overlay must be invisible in the parent. Nothing enforces this at runtime:
/// if it regresses, K lifted workers are writing into one unsynchronized `loki::IndexedHashSet`.
TEST(MimirTests, MimirFormalismProblemGroundingOverlayLeavesParentFrozenTest)
{
    const auto parent = parse_problem("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto [action, binding] = pick_groundable_action(*parent);

    const auto parent_sizes_before = repository_sizes(*parent);
    const auto parent_index_tree_before = parent->get_index_tree_table().size();
    const auto parent_double_leaf_before = parent->get_double_leaf_table().size();

    {
        const auto overlay = ProblemImpl::create_grounding_overlay(parent);
        const auto ground_action = overlay->ground(action, binding);
        ASSERT_NE(ground_action, nullptr);

        /* The overlay did grow -- otherwise this test would pass vacuously. */
        EXPECT_GT(overlay->get_repositories().get_hana_repositories()[boost::hana::type<GroundActionImpl> {}].get_vector().size(), 0u);

        /* ... and the parent did not. */
        EXPECT_EQ(repository_sizes(*parent), parent_sizes_before);
        EXPECT_EQ(parent->get_index_tree_table().size(), parent_index_tree_before);
        EXPECT_EQ(parent->get_double_leaf_table().size(), parent_double_leaf_before);
    }

    /* Destroying an overlay leaves the parent exactly as it was. */
    EXPECT_EQ(repository_sizes(*parent), parent_sizes_before);
}

/// Sibling overlays are independent workspaces: the same ground action is a different object in
/// each, and one overlay's growth is invisible to the other.
TEST(MimirTests, MimirFormalismProblemGroundingOverlaySiblingsAreIndependentTest)
{
    const auto parent = parse_problem("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto [action, binding] = pick_groundable_action(*parent);

    const auto overlay_a = ProblemImpl::create_grounding_overlay(parent);
    const auto overlay_b = ProblemImpl::create_grounding_overlay(parent);

    const auto ground_a = overlay_a->ground(action, binding);
    const auto ground_b = overlay_b->ground(action, binding);

    /* Same schema and binding, hence the same semantics -- but two distinct interned objects, each
       owned by its own overlay. This is exactly why a `GroundAction` must never cross overlays. */
    ASSERT_NE(ground_a, ground_b);
    EXPECT_EQ(ground_a->get_action(), ground_b->get_action());
    EXPECT_EQ(ground_a->get_objects(), ground_b->get_objects());
    EXPECT_EQ(ground_a->get_index(), ground_b->get_index());  ///< same local index, different object

    /* Re-grounding hits each overlay's own grounding table. */
    EXPECT_EQ(overlay_a->ground(action, binding), ground_a);
    EXPECT_EQ(overlay_b->ground(action, binding), ground_b);

    /* Grow one overlay asymmetrically and check each still resolves only its own entries. */
    const auto& actions = parent->get_domain()->get_actions();
    for (const auto& other_action : actions)
    {
        if (other_action == action || other_action->get_arity() > parent->get_problem_and_domain_objects().size())
        {
            continue;
        }
        auto other_binding = ObjectList {};
        for (size_t i = 0; i < other_action->get_arity(); ++i)
        {
            other_binding.push_back(parent->get_problem_and_domain_objects().at(i));
        }
        overlay_a->ground(other_action, other_binding);
    }

    const auto& repositories_a = overlay_a->get_repositories().get_hana_repositories()[boost::hana::type<GroundActionImpl> {}];
    const auto& repositories_b = overlay_b->get_repositories().get_hana_repositories()[boost::hana::type<GroundActionImpl> {}];
    EXPECT_GT(repositories_a.get_vector().size(), repositories_b.get_vector().size());
    EXPECT_EQ(repositories_a[ground_a->get_index()], ground_a);
    EXPECT_EQ(repositories_b[ground_b->get_index()], ground_b);
}

/// Inherited atom indices mean the same thing in the parent and in every overlay. This is what
/// makes a dense fluent-atom description the portable way to move a state into an overlay.
TEST(MimirTests, MimirFormalismProblemGroundingOverlaySharesInheritedAtomIndicesTest)
{
    const auto parent = parse_problem("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto overlay = ProblemImpl::create_grounding_overlay(parent);

    for (const auto& atom : parent->get_fluent_initial_atoms())
    {
        EXPECT_EQ(overlay->get_repositories().get_ground_atom<FluentTag>(atom->get_index()), atom);
    }
}

/// Overlays of overlays are rejected: an overlay's local indices are stamped relative to its
/// parent's size, and an overlay parent grows during search.
TEST(MimirTests, MimirFormalismProblemGroundingOverlayRejectsNestingTest)
{
    const auto parent = parse_problem("gripper/domain.pddl", "gripper/test_problem.pddl");
    const auto overlay = ProblemImpl::create_grounding_overlay(parent);

    EXPECT_THROW((void) ProblemImpl::create_grounding_overlay(overlay), std::runtime_error);
    EXPECT_THROW((void) ProblemImpl::create_grounding_overlay(Problem(nullptr)), std::runtime_error);
}

/// The real acceptance test: a full lifted KPKC stack built against an overlay solves a problem
/// end to end, and the parent is untouched afterwards.
TEST(MimirTests, MimirFormalismProblemGroundingOverlayRunsLiftedSearchTest)
{
    const auto parent = parse_problem("gripper/domain.pddl", "gripper/test_problem.pddl");

    const auto parent_sizes_before = repository_sizes(*parent);
    const auto parent_index_tree_before = parent->get_index_tree_table().size();

    const auto overlay = ProblemImpl::create_grounding_overlay(parent);
    const auto context = create_overlay_context(overlay);

    const auto result = brfs::find_solution(context);

    ASSERT_EQ(result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(result.plan.has_value());

    /* Grounding really happened -- in the overlay. */
    EXPECT_GT(overlay->get_repositories().get_hana_repositories()[boost::hana::type<GroundActionImpl> {}].get_vector().size(), 0u);
    EXPECT_GT(overlay->get_index_tree_table().size(), 0u);

    /* And nowhere else. */
    EXPECT_EQ(repository_sizes(*parent), parent_sizes_before);
    EXPECT_EQ(parent->get_index_tree_table().size(), parent_index_tree_before);
}

/// Two overlays running the same lifted search independently must agree on the answer while
/// staying disjoint, and the plan length must match what the parent's own grounded search finds.
TEST(MimirTests, MimirFormalismProblemGroundingOverlayLiftedSearchesAgreeTest)
{
    const auto parent = parse_problem("gripper/domain.pddl", "gripper/test_problem.pddl");

    const auto reference =
        brfs::find_solution(SearchContextImpl::create(parse_problem("gripper/domain.pddl", "gripper/test_problem.pddl"),
                                                      SearchContextImpl::Options(SearchContextImpl::GroundedOptions())));
    ASSERT_EQ(reference.status, SearchStatus::SOLVED);

    const auto parent_sizes_before = repository_sizes(*parent);

    auto plan_lengths = std::vector<size_t> {};
    for (int k = 0; k < 2; ++k)
    {
        const auto overlay = ProblemImpl::create_grounding_overlay(parent);
        const auto result = brfs::find_solution(create_overlay_context(overlay));
        ASSERT_EQ(result.status, SearchStatus::SOLVED);
        plan_lengths.push_back(result.plan->get_actions().size());
    }

    EXPECT_EQ(plan_lengths[0], plan_lengths[1]);
    EXPECT_EQ(plan_lengths[0], reference.plan->get_actions().size());
    EXPECT_EQ(repository_sizes(*parent), parent_sizes_before);
}
}
