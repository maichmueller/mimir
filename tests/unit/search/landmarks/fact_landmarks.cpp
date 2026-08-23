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

#include "mimir/search/landmarks/fact_landmark_generator.hpp"

#include "mimir/common/filesystem.hpp"
#include "mimir/formalism/action.hpp"
#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/object.hpp"
#include "mimir/formalism/parser.hpp"
#include "mimir/formalism/predicate.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/search/grounders/lifted.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state.hpp"
#include "mimir/search/state_repository.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace mimir::search;
using namespace mimir::search::landmarks;
using namespace mimir::formalism;

namespace mimir::tests
{
namespace
{

bool matches(GroundAtom<FluentTag> atom, const std::string& predicate_name, const std::vector<std::string>& object_names)
{
    if (atom->get_predicate()->get_name() != predicate_name)
    {
        return false;
    }
    const auto& objects = atom->get_objects();
    if (objects.size() != object_names.size())
    {
        return false;
    }
    for (size_t i = 0; i < objects.size(); ++i)
    {
        if (objects[i]->get_name() != object_names[i])
        {
            return false;
        }
    }
    return true;
}

bool matches(GroundAction action, const std::string& schema_name, const std::vector<std::string>& object_names)
{
    if (action->get_action()->get_name() != schema_name)
    {
        return false;
    }
    const auto& objects = action->get_objects();
    if (objects.size() != object_names.size())
    {
        return false;
    }
    for (size_t i = 0; i < objects.size(); ++i)
    {
        if (objects[i]->get_name() != object_names[i])
        {
            return false;
        }
    }
    return true;
}

GroundAtom<FluentTag> find_landmark(const FactLandmarkGraph& landmarks, const std::string& predicate_name, const std::vector<std::string>& object_names)
{
    for (const auto atom : landmarks->get_landmark_atoms())
    {
        if (matches(atom, predicate_name, object_names))
        {
            return atom;
        }
    }
    return nullptr;
}

GroundAtom<FluentTag> resolve_atom(const FactLandmarkGraph& landmarks, Index atom_index)
{
    return landmarks->get_problem()->get_repositories().get_ground_atom<FluentTag>(atom_index);
}

bool contains_atom(const FactLandmarkGraph& landmarks, const IndexList& atom_indices, const std::string& predicate_name, const std::vector<std::string>& object_names)
{
    return std::any_of(atom_indices.begin(),
                       atom_indices.end(),
                       [&](Index idx) { return matches(resolve_atom(landmarks, idx), predicate_name, object_names); });
}

bool contains_action(const std::vector<GroundAction>& actions, const std::string& schema_name, const std::vector<std::string>& object_names)
{
    return std::any_of(actions.begin(), actions.end(), [&](GroundAction action) { return matches(action, schema_name, object_names); });
}

bool has_duplicates(IndexList indices)
{
    std::sort(indices.begin(), indices.end());
    return std::adjacent_find(indices.begin(), indices.end()) != indices.end();
}

Problem parse(const std::string& domain_name, const std::string& problem_filename = "test_problem.pddl")
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + domain_name + "/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + domain_name + "/" + problem_filename);
    auto parser = Parser(domain_file);
    parser.get_domain();
    return parser.parse_problem(problem_file);
}

}

TEST(MimirTests, SearchLandmarksBlocks4GoalCoverageAndUniqueAchieverTest)
{
    const auto problem = parse("blocks_4");
    const auto grounder = LiftedGrounder(problem);
    const auto landmarks = ApproximateFactLandmarkGenerator::create(grounder);

    // All 5 positive fluent goal atoms must be landmarks.
    for (const auto& [predicate_name, object_names] :
        std::vector<std::pair<std::string, std::vector<std::string>>> {
            { "clear", { "b2" } }, { "on", { "b2", "b3" } }, { "on-table", { "b3" } }, { "clear", { "b1" } }, { "on-table", { "b1" } } })
    {
        const auto atom = find_landmark(landmarks, predicate_name, object_names);
        ASSERT_NE(atom, nullptr) << predicate_name;
        EXPECT_TRUE(landmarks->is_landmark(atom));
    }

    // `stack` is the only action schema that ever adds `on`, and only one grounding (b2, b3) adds
    // this specific atom, so it must be a unique achiever.
    const auto on_b2_b3 = find_landmark(landmarks, "on", { "b2", "b3" });
    ASSERT_NE(on_b2_b3, nullptr);
    const auto achievers = landmarks->get_achievers(on_b2_b3->get_index());
    ASSERT_EQ(achievers.size(), 1u);
    EXPECT_TRUE(matches(achievers.front(), "stack", { "b2", "b3" }));

    const auto unique_achiever = landmarks->get_unique_achiever(on_b2_b3->get_index());
    ASSERT_TRUE(unique_achiever.has_value());
    EXPECT_TRUE(matches(*unique_achiever, "stack", { "b2", "b3" }));
    EXPECT_TRUE(landmarks->is_unique_landmark_achiever(*unique_achiever));
    EXPECT_TRUE(landmarks->is_landmark_achiever(*unique_achiever));
    EXPECT_TRUE(landmarks->is_first_landmark_achiever(*unique_achiever));

    // stack(b2, b3)'s only preconditions are (clear b3) and (holding b2): with a single achiever,
    // the necessary-predecessor intersection is exactly its own precondition set.
    const auto& predecessors = landmarks->get_predecessors(on_b2_b3->get_index());
    EXPECT_EQ(predecessors.size(), 2u);
    EXPECT_TRUE(contains_atom(landmarks, predecessors, "clear", { "b3" }));
    EXPECT_TRUE(contains_atom(landmarks, predecessors, "holding", { "b2" }));

    // Both predecessors must themselves be landmarks discovered by propagation (not goal atoms).
    const auto clear_b3 = find_landmark(landmarks, "clear", { "b3" });
    const auto holding_b2 = find_landmark(landmarks, "holding", { "b2" });
    ASSERT_NE(clear_b3, nullptr);
    ASSERT_NE(holding_b2, nullptr);

    // Edge symmetry: on(b2,b3) must appear among clear(b3)'s and holding(b2)'s successors.
    EXPECT_TRUE(contains_atom(landmarks, landmarks->get_successors(clear_b3->get_index()), "on", { "b2", "b3" }));
    EXPECT_TRUE(contains_atom(landmarks, landmarks->get_successors(holding_b2->get_index()), "on", { "b2", "b3" }));
}

TEST(MimirTests, SearchLandmarksGripperNonUniqueAchieverTest)
{
    const auto problem = parse("gripper");
    const auto grounder = LiftedGrounder(problem);
    const auto landmarks = ApproximateFactLandmarkGenerator::create(grounder);

    const auto at_ball2_roomb = find_landmark(landmarks, "at", { "ball2", "roomb" });
    ASSERT_NE(at_ball2_roomb, nullptr);
    EXPECT_TRUE(landmarks->is_landmark(at_ball2_roomb));

    // Two grippers can each carry ball2 into roomb: exactly two, non-unique, first achievers.
    const auto achievers = landmarks->get_achievers(at_ball2_roomb->get_index());
    ASSERT_EQ(achievers.size(), 2u);
    EXPECT_TRUE(contains_action(achievers, "drop", { "ball2", "roomb", "left" }));
    EXPECT_TRUE(contains_action(achievers, "drop", { "ball2", "roomb", "right" }));

    const auto first_achievers = landmarks->get_first_achievers(at_ball2_roomb->get_index());
    EXPECT_EQ(first_achievers.size(), 2u);

    EXPECT_EQ(landmarks->get_unique_achiever_action_index(at_ball2_roomb->get_index()), std::nullopt);
    for (const auto action : achievers)
    {
        EXPECT_FALSE(landmarks->is_unique_landmark_achiever(action));
        EXPECT_TRUE(landmarks->is_landmark_achiever(action));
    }

    // The necessary-precondition intersection must drop gripper identity: only the shared
    // (at-robby roomb) precondition survives, not either gripper's (carry ball2 ?).
    const auto& predecessors = landmarks->get_predecessors(at_ball2_roomb->get_index());
    EXPECT_EQ(predecessors.size(), 1u);
    EXPECT_TRUE(contains_atom(landmarks, predecessors, "at-robby", { "roomb" }));
    EXPECT_FALSE(contains_atom(landmarks, predecessors, "carry", { "ball2", "left" }));
    EXPECT_FALSE(contains_atom(landmarks, predecessors, "carry", { "ball2", "right" }));
}

TEST(MimirTests, SearchLandmarksConditionalEffectAchieverDedupTest)
{
    const auto problem = parse("landmark_cond_effect_dedup");
    const auto grounder = LiftedGrounder(problem);
    const auto landmarks = ApproximateFactLandmarkGenerator::create(grounder);

    // `act` has two conditional effects (triggered by s1, s2 respectively) that both add `r`, but
    // it is a single grounded action (zero parameters): achievers must collapse to one entry.
    const auto r = find_landmark(landmarks, "r", {});
    ASSERT_NE(r, nullptr);

    const auto achievers = landmarks->get_achievers(r->get_index());
    ASSERT_EQ(achievers.size(), 1u);
    EXPECT_TRUE(matches(achievers.front(), "act", {}));

    const auto first_achievers = landmarks->get_first_achievers(r->get_index());
    ASSERT_EQ(first_achievers.size(), 1u);
    EXPECT_TRUE(matches(first_achievers.front(), "act", {}));

    EXPECT_TRUE(landmarks->is_unique_landmark_achiever(achievers.front()));

    // (X ∪ A) ∩ (X ∪ B) = X ∪ (A ∩ B) with X={p} (shared base precondition), A={s1}, B={s2},
    // A ∩ B = ∅: only `p` should survive as a necessary predecessor, not s1 or s2.
    const auto& predecessors = landmarks->get_predecessors(r->get_index());
    EXPECT_EQ(predecessors.size(), 1u);
    EXPECT_TRUE(contains_atom(landmarks, predecessors, "p", {}));
    EXPECT_FALSE(contains_atom(landmarks, predecessors, "s1", {}));
    EXPECT_FALSE(contains_atom(landmarks, predecessors, "s2", {}));
}

TEST(MimirTests, SearchLandmarksGraphIntegrityTest)
{
    const auto problem = parse("blocks_4");
    const auto grounder = LiftedGrounder(problem);
    const auto landmarks = ApproximateFactLandmarkGenerator::create(grounder);

    ASSERT_FALSE(landmarks->get_landmark_atom_indices().empty());

    for (const auto atom_index : landmarks->get_landmark_atom_indices())
    {
        EXPECT_FALSE(has_duplicates(landmarks->get_achiever_action_indices(atom_index)));
        EXPECT_FALSE(has_duplicates(landmarks->get_first_achiever_action_indices(atom_index)));
        EXPECT_FALSE(has_duplicates(landmarks->get_predecessors(atom_index)));
        EXPECT_FALSE(has_duplicates(landmarks->get_successors(atom_index)));

        for (const auto predecessor_index : landmarks->get_predecessors(atom_index))
        {
            EXPECT_TRUE(landmarks->is_landmark(predecessor_index));
            const auto& predecessor_successors = landmarks->get_successors(predecessor_index);
            EXPECT_NE(std::find(predecessor_successors.begin(), predecessor_successors.end(), atom_index), predecessor_successors.end());
        }
        for (const auto successor_index : landmarks->get_successors(atom_index))
        {
            EXPECT_TRUE(landmarks->is_landmark(successor_index));
        }
    }
}

TEST(MimirTests, SearchLandmarksGreedyNecessaryOrderingsDisabledTest)
{
    const auto problem = parse("blocks_4");
    const auto grounder = LiftedGrounder(problem);

    auto options = FactLandmarkGeneratorOptions();
    options.compute_greedy_necessary_orderings = false;
    const auto landmarks = ApproximateFactLandmarkGenerator::create(grounder, options);

    // Discovery still runs to a fixpoint: more than just the 5 goal atoms are found.
    EXPECT_GT(landmarks->get_landmark_atom_indices().size(), 5u);

    for (const auto atom_index : landmarks->get_landmark_atom_indices())
    {
        EXPECT_TRUE(landmarks->get_predecessors(atom_index).empty());
        EXPECT_TRUE(landmarks->get_successors(atom_index).empty());
    }
}

TEST(MimirTests, SearchLandmarksAchievedUnachievedInitialStateTest)
{
    const auto problem = parse("blocks_4");
    const auto grounder = LiftedGrounder(problem);
    const auto landmarks = ApproximateFactLandmarkGenerator::create(grounder);

    const auto search_options = SearchContextImpl::Options(SearchContextImpl::GroundedOptions());
    const auto search_context = SearchContextImpl::create(problem, search_options);
    const auto initial_state = search_context->get_state_repository()->get_or_create_initial_state().first;

    const auto achieved = landmarks->get_achieved_landmark_atom_indices(initial_state);
    const auto unachieved = landmarks->get_unachieved_landmark_atom_indices(initial_state);

    EXPECT_EQ(achieved.size() + unachieved.size(), landmarks->get_landmark_atom_indices().size());
    EXPECT_TRUE(contains_atom(landmarks, achieved, "clear", { "b2" }));
    EXPECT_TRUE(contains_atom(landmarks, achieved, "clear", { "b1" }));
    EXPECT_TRUE(contains_atom(landmarks, unachieved, "on", { "b2", "b3" }));
    EXPECT_TRUE(contains_atom(landmarks, unachieved, "on-table", { "b1" }));
}

/**
 * Disjunctive landmarks.
 *
 * Gripper is the minimal witness. `(at ball2 roomb)` has exactly two first achievers, `drop ball2
 * roomb left` and `drop ball2 roomb right`, so the precondition *intersection* keeps only
 * `(at-robby roomb)` and discards `(carry ball2 ?g)` -- `SearchLandmarksGripperNonUniqueAchieverTest`
 * asserts that discard directly. The two carry atoms are what a disjunctive landmark recovers.
 */

TEST(MimirTests, SearchLandmarksDisjunctiveOffByDefaultTest)
{
    const auto problem = parse("gripper");
    const auto grounder = LiftedGrounder(problem);
    const auto landmarks = ApproximateFactLandmarkGenerator::create(grounder);

    EXPECT_TRUE(landmarks->get_disjunctive_landmarks().empty());
    EXPECT_TRUE(landmarks->get_disjunctive_landmark_atom_indices().empty());
}

TEST(MimirTests, SearchLandmarksGripperDisjunctiveCarryTest)
{
    const auto problem = parse("gripper");
    const auto grounder = LiftedGrounder(problem);
    auto options = FactLandmarkGeneratorOptions();
    options.max_disjunctive_landmark_size = 4;
    const auto landmarks = ApproximateFactLandmarkGenerator::create(grounder, options);

    // The pair the intersection drops, recovered whole.
    auto found_carry_pair = false;
    for (const auto& members : landmarks->get_disjunctive_landmarks())
    {
        if (members.size() == 2 && contains_atom(landmarks, members, "carry", { "ball2", "left" })
            && contains_atom(landmarks, members, "carry", { "ball2", "right" }))
        {
            found_carry_pair = true;
        }
    }
    EXPECT_TRUE(found_carry_pair);

    // ... and it stays out of the fact landmarks, which is the split the graph exists to keep.
    EXPECT_FALSE(contains_atom(landmarks, landmarks->get_landmark_atom_indices(), "carry", { "ball2", "left" }));
    EXPECT_FALSE(contains_atom(landmarks, landmarks->get_landmark_atom_indices(), "carry", { "ball2", "right" }));
    EXPECT_TRUE(contains_atom(landmarks, landmarks->get_disjunctive_landmark_atom_indices(), "carry", { "ball2", "left" }));
}

TEST(MimirTests, SearchLandmarksDisjunctiveWellFormedTest)
{
    const auto problem = parse("gripper");
    const auto grounder = LiftedGrounder(problem);
    auto options = FactLandmarkGeneratorOptions();
    options.max_disjunctive_landmark_size = 4;
    const auto landmarks = ApproximateFactLandmarkGenerator::create(grounder, options);

    const auto& disjunctive = landmarks->get_disjunctive_landmarks();
    ASSERT_FALSE(disjunctive.empty());

    auto seen = std::set<IndexList> {};
    auto union_of_members = IndexList {};
    for (const auto& members : disjunctive)
    {
        EXPECT_FALSE(members.empty());
        EXPECT_LE(members.size(), options.max_disjunctive_landmark_size);
        EXPECT_TRUE(std::is_sorted(members.begin(), members.end()));
        EXPECT_FALSE(has_duplicates(members));
        EXPECT_TRUE(seen.insert(members).second) << "duplicate disjunctive landmark";

        // Subsumption: a set holding a fact landmark says nothing that landmark does not.
        for (const auto member : members)
        {
            EXPECT_FALSE(landmarks->is_landmark(member));
        }
        union_of_members.insert(union_of_members.end(), members.begin(), members.end());
    }

    std::sort(union_of_members.begin(), union_of_members.end());
    union_of_members.erase(std::unique(union_of_members.begin(), union_of_members.end()), union_of_members.end());
    EXPECT_EQ(landmarks->get_disjunctive_landmark_atom_indices(), union_of_members);
}

TEST(MimirTests, SearchLandmarksDisjunctiveSizeCapTest)
{
    const auto problem = parse("gripper");
    const auto grounder = LiftedGrounder(problem);

    auto capped = FactLandmarkGeneratorOptions();
    capped.max_disjunctive_landmark_size = 1;
    const auto narrow = ApproximateFactLandmarkGenerator::create(grounder, capped);

    auto wide_options = FactLandmarkGeneratorOptions();
    wide_options.max_disjunctive_landmark_size = 4;
    const auto wide = ApproximateFactLandmarkGenerator::create(grounder, wide_options);

    // A cap of 1 admits only sets the intersection could already have produced, so the two-element
    // carry pair is gone while the wider run keeps it.
    for (const auto& members : narrow->get_disjunctive_landmarks())
    {
        EXPECT_EQ(members.size(), 1u);
    }
    EXPECT_FALSE(contains_atom(narrow, narrow->get_disjunctive_landmark_atom_indices(), "carry", { "ball2", "left" }));
    EXPECT_TRUE(contains_atom(wide, wide->get_disjunctive_landmark_atom_indices(), "carry", { "ball2", "left" }));
}

TEST(MimirTests, SearchLandmarksDisjunctiveDepthBoundTest)
{
    const auto problem = parse("gripper");
    const auto grounder = LiftedGrounder(problem);

    auto shallow = FactLandmarkGeneratorOptions();
    shallow.max_disjunctive_landmark_size = 4;
    shallow.max_disjunctive_landmark_depth = 1;

    auto unbounded = FactLandmarkGeneratorOptions();
    unbounded.max_disjunctive_landmark_size = 4;

    const auto shallow_graph = ApproximateFactLandmarkGenerator::create(grounder, shallow);
    const auto unbounded_graph = ApproximateFactLandmarkGenerator::create(grounder, unbounded);

    EXPECT_LE(shallow_graph->get_disjunctive_landmark_atom_indices().size(), unbounded_graph->get_disjunctive_landmark_atom_indices().size());
}

TEST(MimirTests, SearchLandmarksDisjunctiveLeavesFactLandmarksUntouchedTest)
{
    const auto problem = parse("blocks_4");
    const auto grounder = LiftedGrounder(problem);

    const auto without = ApproximateFactLandmarkGenerator::create(grounder);
    auto options = FactLandmarkGeneratorOptions();
    options.max_disjunctive_landmark_size = 4;
    const auto with = ApproximateFactLandmarkGenerator::create(grounder, options);

    // The whole point of keeping the two sets apart: turning disjunctive landmarks on must not
    // move a single fact landmark, or the two settings become one flag.
    EXPECT_EQ(without->get_landmark_atom_indices(), with->get_landmark_atom_indices());
    for (const auto atom_index : with->get_landmark_atom_indices())
    {
        EXPECT_EQ(without->get_predecessors(atom_index), with->get_predecessors(atom_index));
        EXPECT_EQ(without->get_achiever_action_indices(atom_index), with->get_achiever_action_indices(atom_index));
    }
}

}
