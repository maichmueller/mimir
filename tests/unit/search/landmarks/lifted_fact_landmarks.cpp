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

#include "mimir/search/landmarks/lifted_fact_landmark_generator.hpp"

#include "mimir/common/filesystem.hpp"
#include "mimir/formalism/action.hpp"
#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/ground_conjunctive_condition.hpp"
#include "mimir/formalism/ground_effects.hpp"
#include "mimir/formalism/object.hpp"
#include "mimir/formalism/type.hpp"
#include "mimir/formalism/parser.hpp"
#include "mimir/formalism/predicate.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/algorithms/iw.hpp"
#include "mimir/search/algorithms/strategies/transition_ordering_strategy.hpp"
#include "mimir/search/grounders/lifted.hpp"
#include "mimir/search/landmarks/fact_landmark_generator.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"
#include "mimir/search/plan.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state.hpp"
#include "mimir/search/state_repository.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <tuple>
#include <set>
#include <string>
#include <unordered_set>
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

GroundAtom<FluentTag> resolve_atom(const FactLandmarkGraph& landmarks, Index atom_index)
{
    return landmarks->get_problem()->get_repositories().get_ground_atom<FluentTag>(atom_index);
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

bool contains_atom(const FactLandmarkGraph& landmarks,
                   const IndexList& atom_indices,
                   const std::string& predicate_name,
                   const std::vector<std::string>& object_names)
{
    return std::any_of(atom_indices.begin(),
                       atom_indices.end(),
                       [&](Index idx) { return matches(resolve_atom(landmarks, idx), predicate_name, object_names); });
}

/// @brief The lifted landmark rendered as `predicate(a, ?, ...)`, or `nullptr` if absent.
const LiftedLandmark* find_lifted(const FactLandmarkGraph& landmarks, const std::string& rendered)
{
    for (const auto& record : landmarks->get_lifted_landmarks())
    {
        if (to_string(record) == rendered)
        {
            return &record;
        }
    }
    return nullptr;
}

std::vector<IndexList> sets_over_predicate(const FactLandmarkGraph& landmarks, const std::string& predicate_name)
{
    auto result = std::vector<IndexList> {};
    for (const auto& members : landmarks->get_disjunctive_landmarks())
    {
        if (!members.empty() && resolve_atom(landmarks, members.front())->get_predicate()->get_name() == predicate_name)
        {
            result.push_back(members);
        }
    }
    return result;
}

std::string atom_signature(GroundAtom<FluentTag> atom)
{
    auto rendered = atom->get_predicate()->get_name() + "(";
    for (size_t i = 0; i < atom->get_objects().size(); ++i)
    {
        rendered += (i ? ", " : "") + atom->get_objects()[i]->get_name();
    }
    return rendered + ")";
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

size_t num_fluent_atoms(const Problem& problem)
{
    return boost::hana::at_key(problem->get_repositories().get_hana_repositories(), boost::hana::type<GroundAtomImpl<FluentTag>> {}).size();
}

/**
 * The soundness oracle (T9).
 *
 * A landmark claims that every plan passes through a state containing one of its members. Every
 * rule this generator applies argues over an arbitrary plan and stays valid under the delete
 * relaxation, so each landmark must also be a landmark of Pi+ -- and *that* is decidable: delete
 * every ground action able to produce a member and ask whether the goal is still relaxed-reachable.
 * If it is, some relaxed plan avoids the landmark entirely and the derivation was wrong.
 */

/// @brief One conditional effect of one ground action, as a delete-relaxed production rule.
struct RelaxedRule
{
    Index action_index;
    IndexList preconditions;
    IndexList adds;
};

class RelaxedTask
{
public:
    RelaxedTask(const Problem& problem, const GroundActionList& actions) : m_num_atoms(num_fluent_atoms(problem))
    {
        for (const auto action : actions)
        {
            auto base_preconditions = IndexList {};
            for (const auto atom_index : action->get_conjunctive_condition()->get_precondition<PositiveTag, FluentTag>())
            {
                base_preconditions.push_back(atom_index);
            }

            for (const auto conditional_effect : action->get_conditional_effects())
            {
                auto rule = RelaxedRule { action->get_index(), base_preconditions, {} };
                for (const auto atom_index : conditional_effect->get_conjunctive_condition()->get_precondition<PositiveTag, FluentTag>())
                {
                    rule.preconditions.push_back(atom_index);
                }
                for (const auto atom_index : conditional_effect->get_conjunctive_effect()->get_propositional_effects<PositiveTag>())
                {
                    rule.adds.push_back(atom_index);
                }
                std::sort(rule.preconditions.begin(), rule.preconditions.end());
                rule.preconditions.erase(std::unique(rule.preconditions.begin(), rule.preconditions.end()), rule.preconditions.end());
                m_rules.push_back(std::move(rule));
            }
        }

        m_num_atoms = std::max(m_num_atoms, num_fluent_atoms(problem));
        m_rules_by_precondition.resize(m_num_atoms);
        m_rules_by_add.resize(m_num_atoms);
        for (size_t r = 0; r < m_rules.size(); ++r)
        {
            for (const auto atom_index : m_rules[r].preconditions)
            {
                m_rules_by_precondition[atom_index].push_back(Index(r));
            }
            for (const auto atom_index : m_rules[r].adds)
            {
                m_rules_by_add[atom_index].push_back(Index(r));
            }
        }

        for (const auto atom : problem->get_fluent_initial_atoms())
        {
            m_initial_atoms.push_back(atom->get_index());
        }
        for (const auto atom_index : problem->get_goal_condition()->get_precondition<PositiveTag, FluentTag>())
        {
            m_goal_atoms.push_back(atom_index);
        }
    }

    /// @brief Is the goal delete-relaxed reachable with every action that produces a `forbidden`
    /// atom removed?
    bool goal_reachable_without(const IndexList& forbidden) const
    {
        auto disabled_actions = std::unordered_set<Index> {};
        for (const auto atom_index : forbidden)
        {
            if (atom_index >= m_rules_by_add.size())
            {
                continue;
            }
            for (const auto rule_index : m_rules_by_add[atom_index])
            {
                disabled_actions.insert(m_rules[rule_index].action_index);
            }
        }

        auto unsatisfied = std::vector<size_t>(m_rules.size());
        for (size_t r = 0; r < m_rules.size(); ++r)
        {
            unsatisfied[r] = m_rules[r].preconditions.size();
        }

        auto reached = std::vector<char>(m_num_atoms, 0);
        auto queue = IndexList {};
        const auto reach = [&](Index atom_index)
        {
            if (atom_index < m_num_atoms && !reached[atom_index])
            {
                reached[atom_index] = 1;
                queue.push_back(atom_index);
            }
        };

        for (const auto atom_index : m_initial_atoms)
        {
            reach(atom_index);
        }
        for (size_t r = 0; r < m_rules.size(); ++r)
        {
            if (unsatisfied[r] == 0 && !disabled_actions.count(m_rules[r].action_index))
            {
                for (const auto atom_index : m_rules[r].adds)
                {
                    reach(atom_index);
                }
            }
        }

        while (!queue.empty())
        {
            const auto atom_index = queue.back();
            queue.pop_back();
            for (const auto rule_index : m_rules_by_precondition[atom_index])
            {
                if (--unsatisfied[rule_index] == 0 && !disabled_actions.count(m_rules[rule_index].action_index))
                {
                    for (const auto add_index : m_rules[rule_index].adds)
                    {
                        reach(add_index);
                    }
                }
            }
        }

        return std::all_of(m_goal_atoms.begin(), m_goal_atoms.end(), [&](Index atom_index) { return atom_index < m_num_atoms && reached[atom_index]; });
    }

    const IndexList& get_initial_atoms() const { return m_initial_atoms; }

private:
    size_t m_num_atoms;
    std::vector<RelaxedRule> m_rules;
    std::vector<IndexList> m_rules_by_precondition;
    std::vector<IndexList> m_rules_by_add;
    IndexList m_initial_atoms;
    IndexList m_goal_atoms;
};

/// @brief Number of landmarks the oracle refutes, and how many it could test.
struct OracleResult
{
    size_t num_tested = 0;
    size_t num_failed = 0;
};

/// @brief Run the oracle over every landmark none of whose members is true initially. `goal_atoms`
/// are excluded: a goal atom is a landmark by definition and removing its achievers trivially makes
/// the goal unreachable, so testing it measures nothing.
OracleResult run_oracle(const FactLandmarkGraph& landmarks, const RelaxedTask& task, const std::string& label, bool report_failures)
{
    const auto initial = std::unordered_set<Index>(task.get_initial_atoms().begin(), task.get_initial_atoms().end());
    const auto goal_atoms = std::unordered_set<Index>(landmarks->get_problem()->get_goal_condition()->get_precondition<PositiveTag, FluentTag>().begin(),
                                                      landmarks->get_problem()->get_goal_condition()->get_precondition<PositiveTag, FluentTag>().end());

    auto candidates = std::vector<IndexList> {};
    for (const auto atom_index : landmarks->get_landmark_atom_indices())
    {
        if (!goal_atoms.count(atom_index))
        {
            candidates.push_back(IndexList { atom_index });
        }
    }
    for (const auto& members : landmarks->get_disjunctive_landmarks())
    {
        candidates.push_back(members);
    }

    auto result = OracleResult {};
    for (const auto& members : candidates)
    {
        if (std::any_of(members.begin(), members.end(), [&](Index atom_index) { return initial.count(atom_index) > 0; }))
        {
            continue;  // trivially satisfied at I: the oracle says nothing about it
        }
        ++result.num_tested;
        if (task.goal_reachable_without(members))
        {
            ++result.num_failed;
            if (report_failures)
            {
                auto rendered = std::string {};
                for (const auto atom_index : members)
                {
                    rendered += (rendered.empty() ? "" : ", ") + resolve_atom(landmarks, atom_index)->get_predicate()->get_name();
                }
                ADD_FAILURE() << label << ": refuted landmark {" << rendered << "} (goal still relaxed-reachable without its achievers)";
            }
        }
    }
    return result;
}

/// @brief The whole graph rendered by *name*, so two runs can be compared byte for byte without
/// depending on the atom indices two separate parses happen to hand out.
std::string render_graph(const FactLandmarkGraph& landmarks)
{
    const auto render_atom = [&](Index atom_index)
    {
        const auto atom = resolve_atom(landmarks, atom_index);
        auto rendered = atom->get_predicate()->get_name() + "(";
        for (size_t i = 0; i < atom->get_objects().size(); ++i)
        {
            rendered += (i ? ", " : "") + atom->get_objects()[i]->get_name();
        }
        return rendered + ")";
    };

    auto out = std::string {};
    out += "facts:\n";
    for (const auto atom_index : landmarks->get_landmark_atom_indices())
    {
        out += "  " + render_atom(atom_index) + "\n";
    }
    out += "sets:\n";
    for (const auto& members : landmarks->get_disjunctive_landmarks())
    {
        out += "  {";
        for (size_t i = 0; i < members.size(); ++i)
        {
            out += (i ? ", " : "") + render_atom(members[i]);
        }
        out += "}\n";
    }
    out += "records:\n";
    for (const auto& record : landmarks->get_lifted_landmarks())
    {
        out += "  " + to_string(record) + (record.initially_true ? " initially_true" : "") + " members=[";
        for (size_t i = 0; i < record.member_atom_indices.size(); ++i)
        {
            out += (i ? ", " : "") + render_atom(record.member_atom_indices[i]);
        }
        out += "] parents=[";
        for (size_t i = 0; i < record.parent_positions.size(); ++i)
        {
            out += (i ? "," : "") + std::to_string(record.parent_positions[i]);
        }
        out += "]\n";
    }
    return out;
}

/// @brief The smallest instance of each shipped IPC domain.
///
/// Read from `data/landmark_ipc_smallest/`, not from `data/ipc/`: the latter is gitignored, so on a
/// clean checkout every test that reached into it failed with "File does not exist".
const std::vector<std::string>& ipc_domains()
{
    static const auto domains = std::vector<std::string> { "blocksworld-ipc", "childsnack-ipc", "ferry-ipc",   "floortile-ipc", "miconic-ipc",
                                                           "rovers-ipc",      "satellite-ipc",  "sokoban-ipc", "spanner-ipc",   "transport-ipc" };
    return domains;
}

}

/**
 * T1: blocks_4 -- the goal atoms, the single-achiever chain, and the pinned difference against the
 * grounded generator.
 */

TEST(MimirTests, SearchLandmarksLiftedBlocks4Test)
{
    const auto problem = parse("blocks_4");
    const auto landmarks = LiftedFactLandmarkGenerator::create(problem);

    for (const auto& [predicate_name, object_names] :
         std::vector<std::pair<std::string, std::vector<std::string>>> {
             { "clear", { "b2" } }, { "on", { "b2", "b3" } }, { "on-table", { "b3" } }, { "clear", { "b1" } }, { "on-table", { "b1" } } })
    {
        EXPECT_NE(find_landmark(landmarks, predicate_name, object_names), nullptr) << predicate_name;
    }

    // `stack` is the only schema adding `on`, so `on(b2,b3)` has one achiever and its whole
    // precondition set is necessary.
    const auto on_b2_b3 = find_landmark(landmarks, "on", { "b2", "b3" });
    ASSERT_NE(on_b2_b3, nullptr);
    const auto& predecessors = landmarks->get_predecessors(on_b2_b3->get_index());
    EXPECT_EQ(predecessors.size(), 2u);
    EXPECT_TRUE(contains_atom(landmarks, predecessors, "clear", { "b3" }));
    EXPECT_TRUE(contains_atom(landmarks, predecessors, "holding", { "b2" }));

    /* `holding(b2)` is added by `pickup(b2)` and by `unstack(b2, ?y)`, and the plain intersection
       over the two keeps only `clear(b2)` and `arm-empty`. §2.7 removes `unstack`: every adder of
       its `on(b2, ?y)` precondition is a `stack(b2, ?)`, which needs `holding(b2)` itself, so the
       instance it uses would have to be initial -- and b2 is on the table in `I`. With `pickup` the
       only possible first achiever, its whole precondition set is necessary. */
    const auto holding_b2 = find_landmark(landmarks, "holding", { "b2" });
    ASSERT_NE(holding_b2, nullptr);
    const auto& holding_predecessors = landmarks->get_predecessors(holding_b2->get_index());
    EXPECT_TRUE(contains_atom(landmarks, holding_predecessors, "clear", { "b2" }));
    EXPECT_TRUE(contains_atom(landmarks, holding_predecessors, "arm-empty", {}));
    EXPECT_TRUE(contains_atom(landmarks, holding_predecessors, "on-table", { "b2" }));

    /* The other half of the same rule. `clear(b3)`'s achievers are `putdown(b3)`, `stack(b3, ?)`
       and `unstack(?x, b3)`; the first two need `holding(b3)`, every adder of which needs
       `clear(b3)` -- so only `unstack` can be first, and `on(?x, b3)` must be initial, which binds
       ?x to the block actually sitting on b3. */
    const auto clear_b3 = find_landmark(landmarks, "clear", { "b3" });
    ASSERT_NE(clear_b3, nullptr);
    const auto& clear_b3_predecessors = landmarks->get_predecessors(clear_b3->get_index());
    EXPECT_TRUE(contains_atom(landmarks, clear_b3_predecessors, "on", { "b1", "b3" }));
    EXPECT_TRUE(contains_atom(landmarks, clear_b3_predecessors, "clear", { "b1" }));
    EXPECT_TRUE(contains_atom(landmarks, clear_b3_predecessors, "arm-empty", {}));

    /* Until §2.7 this test pinned the opposite: `on-table(b2)` and `on(b1,b3)` were the grounded
       generator's *surplus*, atoms it reported because `pickup`/`unstack` beat each other on h_max.
       They are real landmarks, reachable by the RHW exclusion rather than by accident, and on this
       instance the two generators now agree exactly. */
    const auto grounder = LiftedGrounder(problem);
    const auto grounded = ApproximateFactLandmarkGenerator::create(grounder);
    const auto names = [](const FactLandmarkGraph& graph)
    {
        auto result = std::set<std::string> {};
        for (const auto atom : graph->get_landmark_atoms())
        {
            result.insert(atom_signature(atom));
        }
        return result;
    };
    EXPECT_EQ(names(landmarks), names(grounded));
}

/**
 * T2: gripper -- the disjunctive set the precondition intersection has to drop, recovered whole and
 * identical to the grounded generator's (`SearchLandmarksGripperDisjunctiveCarryTest`).
 */

TEST(MimirTests, SearchLandmarksLiftedGripperTest)
{
    const auto problem = parse("gripper");
    const auto landmarks = LiftedFactLandmarkGenerator::create(problem);

    const auto at_ball2_roomb = find_landmark(landmarks, "at", { "ball2", "roomb" });
    ASSERT_NE(at_ball2_roomb, nullptr);

    // `drop(ball2, roomb, ?g)` for either gripper: `at-robby(roomb)` is shared, `carry` is not.
    EXPECT_TRUE(contains_atom(landmarks, landmarks->get_predecessors(at_ball2_roomb->get_index()), "at-robby", { "roomb" }));
    EXPECT_EQ(find_landmark(landmarks, "carry", { "ball2", "left" }), nullptr);

    const auto carry_sets = sets_over_predicate(landmarks, "carry");
    ASSERT_EQ(carry_sets.size(), 1u);
    EXPECT_EQ(carry_sets.front().size(), 2u);
    EXPECT_TRUE(contains_atom(landmarks, carry_sets.front(), "carry", { "ball2", "left" }));
    EXPECT_TRUE(contains_atom(landmarks, carry_sets.front(), "carry", { "ball2", "right" }));

    const auto carry = find_lifted(landmarks, "carry(ball2, ?)");
    ASSERT_NE(carry, nullptr);
    EXPECT_FALSE(carry->fact_atom_index.has_value());
    EXPECT_EQ(carry->member_atom_indices.size(), 2u);

    // Same set as the grounded generator finds.
    const auto grounder = LiftedGrounder(problem);
    auto grounded_options = FactLandmarkGeneratorOptions {};
    grounded_options.max_disjunctive_landmark_size = 4;
    const auto grounded = ApproximateFactLandmarkGenerator::create(grounder, grounded_options);
    const auto grounded_carry_sets = sets_over_predicate(grounded, "carry");
    ASSERT_EQ(grounded_carry_sets.size(), 1u);
    EXPECT_EQ(grounded_carry_sets.front(), carry_sets.front());
}

/**
 * T3: childsnack -- static disambiguation is what makes this domain come out right.
 */

TEST(MimirTests, SearchLandmarksLiftedChildsnackTest)
{
    const auto problem = parse("childsnack");
    const auto landmarks = LiftedFactLandmarkGenerator::create(problem);

    std::cout << "childsnack/test_problem.pddl lifted landmarks:\n";
    for (const auto& record : landmarks->get_lifted_landmarks())
    {
        std::cout << "  " << to_string(record) << " members=" << record.member_atom_indices.size()
                  << (record.fact_atom_index.has_value() ? " fact" : " disjunctive") << (record.initially_true ? " initially_true" : "") << '\n';
    }

    // `child1` is *not* allergic, so `serve_sandwich_no_gluten` is statically impossible and
    // nothing ever requires a gluten-free sandwich.
    EXPECT_TRUE(sets_over_predicate(landmarks, "no_gluten_sandwich").empty());
    EXPECT_EQ(find_landmark(landmarks, "no_gluten_sandwich", { "sandw1" }), nullptr);

    // One tray, one sandwich: every set collapses to a singleton and is promoted to a fact.
    EXPECT_NE(find_landmark(landmarks, "served", { "child1" }), nullptr);
    EXPECT_NE(find_landmark(landmarks, "at", { "tray1", "table1" }), nullptr);
    EXPECT_NE(find_landmark(landmarks, "ontray", { "sandw1", "tray1" }), nullptr);
    EXPECT_NE(find_landmark(landmarks, "at_kitchen_sandwich", { "sandw1" }), nullptr);

    const auto at_kitchen = find_lifted(landmarks, "at(tray1, kitchen)");
    ASSERT_NE(at_kitchen, nullptr);
    EXPECT_TRUE(at_kitchen->initially_true);

    /* The IPC instance has eight children, four of them allergic, two trays and three tables --
       enough for the partial landmarks to stay partial. */
    const auto ipc_problem = parse("landmark_ipc_smallest/childsnack-ipc", "p69.pddl");
    const auto ipc_landmarks = LiftedFactLandmarkGenerator::create(ipc_problem);

    std::cout << "childsnack-ipc p69 lifted landmarks:\n";
    for (const auto& record : ipc_landmarks->get_lifted_landmarks())
    {
        std::cout << "  " << to_string(record) << " members=" << record.member_atom_indices.size()
                  << (record.fact_atom_index.has_value() ? " fact" : " disjunctive") << (record.initially_true ? " initially_true" : "") << '\n';
    }

    // The fact landmarks are exactly the eight goal atoms: no atom is individually mandatory here.
    EXPECT_EQ(ipc_landmarks->get_landmark_atom_indices().size(), 8u);

    // `child2` is allergic and `child1` is not: only the allergic one forces a gluten-free sandwich.
    const auto no_gluten_sets = sets_over_predicate(ipc_landmarks, "no_gluten_sandwich");
    ASSERT_EQ(no_gluten_sets.size(), 1u);
    const auto no_gluten = find_lifted(ipc_landmarks, "no_gluten_sandwich(?)");
    ASSERT_NE(no_gluten, nullptr);
    const auto served_child2 = find_landmark(ipc_landmarks, "served", { "child2" });
    const auto served_child1 = find_landmark(ipc_landmarks, "served", { "child1" });
    ASSERT_NE(served_child2, nullptr);
    ASSERT_NE(served_child1, nullptr);
    const auto& records = ipc_landmarks->get_lifted_landmarks();
    auto no_gluten_parents = std::set<std::string> {};
    for (const auto parent_position : no_gluten->parent_positions)
    {
        no_gluten_parents.insert(to_string(records[parent_position]));
    }
    EXPECT_TRUE(no_gluten_parents.count("served(child2)") > 0);
    EXPECT_TRUE(no_gluten_parents.count("served(child1)") == 0);

    // `waiting(child1, table1)` is static, so the place is disambiguated and only the tray stays
    // free: the members are every tray at that one place.
    const auto at_table1 = find_lifted(ipc_landmarks, "at(?, table1)");
    ASSERT_NE(at_table1, nullptr);
    EXPECT_EQ(at_table1->member_atom_indices.size(), 2u);
    EXPECT_TRUE(contains_atom(ipc_landmarks, at_table1->member_atom_indices, "at", { "tray1", "table1" }));
    EXPECT_TRUE(contains_atom(ipc_landmarks, at_table1->member_atom_indices, "at", { "tray2", "table1" }));

    // One `ontray` set, not one per (sandwich, tray) pair; and one `at_kitchen_sandwich` set below it.
    EXPECT_EQ(sets_over_predicate(ipc_landmarks, "ontray").size(), 1u);
    const auto ontray = find_lifted(ipc_landmarks, "ontray(?, ?)");
    ASSERT_NE(ontray, nullptr);
    EXPECT_EQ(ontray->member_atom_indices.size(), 22u);  // 11 sandwiches x 2 trays

    EXPECT_EQ(sets_over_predicate(ipc_landmarks, "at_kitchen_sandwich").size(), 1u);
    const auto at_kitchen_sandwich = find_lifted(ipc_landmarks, "at_kitchen_sandwich(?)");
    ASSERT_NE(at_kitchen_sandwich, nullptr);
    ASSERT_EQ(at_kitchen_sandwich->parent_positions.size(), 1u);
    EXPECT_EQ(to_string(records[at_kitchen_sandwich->parent_positions.front()]), "ontray(?, ?)");

    // `at(?, kitchen)` is recorded, initially true, and nothing is derived through it.
    const auto ipc_at_kitchen = find_lifted(ipc_landmarks, "at(?, kitchen)");
    ASSERT_NE(ipc_at_kitchen, nullptr);
    EXPECT_TRUE(ipc_at_kitchen->initially_true);
    const auto ipc_at_kitchen_position = Index(ipc_at_kitchen - records.data());
    for (const auto& record : records)
    {
        EXPECT_EQ(std::find(record.parent_positions.begin(), record.parent_positions.end(), ipc_at_kitchen_position), record.parent_positions.end())
            << to_string(record) << " was derived through the initially-true at(?, kitchen)";
    }
}

/**
 * T4: the static filter.
 */

TEST(MimirTests, SearchLandmarksLiftedStaticFilterTest)
{
    auto without_filter = LiftedFactLandmarkGeneratorOptions {};
    without_filter.use_static_filter = false;

    {
        const auto problem = parse("landmark_lifted_static");

        // `achieve-slow` needs `enabled(t1)`, which the instance does not contain, so `achieve-fast`
        // is the only achiever left and both of its preconditions are mandatory.
        const auto filtered = LiftedFactLandmarkGenerator::create(problem);
        EXPECT_NE(find_landmark(filtered, "p", { "t1" }), nullptr);
        EXPECT_NE(find_landmark(filtered, "ready", { "t1" }), nullptr);

        // Without the filter the intersection has to hold over an achiever that can never fire, and
        // `p` -- which only `achieve-fast` needs -- is lost. `ready` survives, but only because
        // §2.5's per-member static test (which is part of the member definition, not of this
        // option) leaves `achieve-slow` contributing nothing and the singleton is promoted back.
        const auto unfiltered = LiftedFactLandmarkGenerator::create(problem, without_filter);
        EXPECT_EQ(find_landmark(unfiltered, "p", { "t1" }), nullptr);
        EXPECT_LT(unfiltered->get_landmark_atom_indices().size(), filtered->get_landmark_atom_indices().size());
    }
    {
        // The disambiguation half, on the domain it was designed for. With the filter, `?c := child1`
        // makes `waiting(child1, ?p)` bind the place and rules out one of the two `serve` schemas;
        // without it, the place stays free and the two schemas share no gluten predicate at all.
        const auto problem = parse("landmark_ipc_smallest/childsnack-ipc", "p69.pddl");

        const auto filtered = LiftedFactLandmarkGenerator::create(problem);
        EXPECT_NE(find_lifted(filtered, "at(?, table1)"), nullptr);
        EXPECT_EQ(find_lifted(filtered, "at(?, ?)"), nullptr);
        EXPECT_EQ(sets_over_predicate(filtered, "no_gluten_sandwich").size(), 1u);

        const auto unfiltered = LiftedFactLandmarkGenerator::create(problem, without_filter);
        EXPECT_EQ(find_lifted(unfiltered, "at(?, table1)"), nullptr);
        EXPECT_NE(find_lifted(unfiltered, "at(?, ?)"), nullptr);
        EXPECT_TRUE(sets_over_predicate(unfiltered, "no_gluten_sandwich").empty());
    }
}

/**
 * T5: occurrence combinations.
 */

TEST(MimirTests, SearchLandmarksLiftedOccurrenceCombinationsTest)
{
    const auto problem = parse("landmark_lifted_occurrences");

    const auto all = LiftedFactLandmarkGenerator::create(problem);
    // `act-two` needs `(q a ?x)` and `(q ?y b)`, `act-one` needs `(q a b)`: one choice vector agrees
    // on the first position, the other on the second, and both are landmarks.
    EXPECT_NE(find_lifted(all, "q(a, ?)"), nullptr);
    EXPECT_NE(find_lifted(all, "q(?, b)"), nullptr);
    EXPECT_EQ(sets_over_predicate(all, "q").size(), 2u);

    auto options = LiftedFactLandmarkGeneratorOptions {};
    options.max_occurrence_combinations = 1;
    const auto first_only = LiftedFactLandmarkGenerator::create(problem, options);
    // Which occurrence comes first is the repository's literal order, so assert the count rather
    // than the identity: exactly one of the two choice landmarks survives.
    EXPECT_EQ(sets_over_predicate(first_only, "q").size(), 1u);
    EXPECT_NE(find_lifted(first_only, "q(a, ?)") != nullptr, find_lifted(first_only, "q(?, b)") != nullptr);
}

/**
 * T6: conditional effects.
 */

TEST(MimirTests, SearchLandmarksLiftedConditionalEffectTest)
{
    {
        const auto problem = parse("landmark_cond_effect_dedup");
        const auto landmarks = LiftedFactLandmarkGenerator::create(problem);

        // `act` adds `r` from two conditional effects. They are two achievers, so the intersection
        // is {p} ∪ ({s1} ∩ {s2}) = {p}; folding them into one achiever would make s1 and s2
        // landmarks, which they are not.
        EXPECT_NE(find_landmark(landmarks, "p", {}), nullptr);
        EXPECT_EQ(find_landmark(landmarks, "s1", {}), nullptr);
        EXPECT_EQ(find_landmark(landmarks, "s2", {}), nullptr);
        EXPECT_TRUE(landmarks->get_disjunctive_landmarks().empty());
    }
    {
        const auto problem = parse("landmark_lifted_cond_effect");
        const auto landmarks = LiftedFactLandmarkGenerator::create(problem);

        // `target` has a single achiever whose add sits inside a `when`: the effect condition
        // `guard` joins the precondition intersection and comes out a landmark.
        const auto target = find_landmark(landmarks, "target", {});
        ASSERT_NE(target, nullptr);
        EXPECT_TRUE(contains_atom(landmarks, landmarks->get_predecessors(target->get_index()), "guard", {}));
        EXPECT_TRUE(contains_atom(landmarks, landmarks->get_predecessors(target->get_index()), "base", {}));

        // `other` is added unconditionally by one action and conditionally by another. Both count,
        // so only their shared `base` survives -- `shared` would be a landmark if the conditional
        // add were not an achiever of its own.
        EXPECT_NE(find_landmark(landmarks, "base", {}), nullptr);
        EXPECT_EQ(find_landmark(landmarks, "shared", {}), nullptr);
        EXPECT_EQ(find_landmark(landmarks, "extra", {}), nullptr);
    }
}

/**
 * T7: the initially-true stop.
 */

TEST(MimirTests, SearchLandmarksLiftedInitiallyTrueStopTest)
{
    auto instances = std::vector<std::pair<std::string, std::string>> { { "blocks_4", "test_problem.pddl" },
                                                                       { "gripper", "test_problem.pddl" },
                                                                       { "delivery", "test_problem.pddl" },
                                                                       { "childsnack", "test_problem.pddl" } };
    for (const auto& domain : ipc_domains())
    {
        instances.emplace_back("landmark_ipc_smallest/" + domain, "p69.pddl");
    }

    auto total_initially_true = size_t(0);
    for (const auto& [domain, instance] : instances)
    {
        const auto problem = parse(domain, instance);
        const auto landmarks = LiftedFactLandmarkGenerator::create(problem);
        const auto& records = landmarks->get_lifted_landmarks();

        for (Index position = 0; position < records.size(); ++position)
        {
            if (!records[position].initially_true)
            {
                continue;
            }
            ++total_initially_true;

            /* The flag is recomputed at assembly from the *final* member union, while the decision
               to stop was taken when the record was popped. A record whose set grew afterwards
               could in principle end up flagged with children already derived -- still sound (the
               earlier derivation's set does avoid I, and Proposition 1 only needs one such set),
               but it would make the flag and the graph disagree. Swept over every shipped instance
               so the day it happens is the day this fails. */
            for (const auto& other : records)
            {
                EXPECT_EQ(std::find(other.parent_positions.begin(), other.parent_positions.end(), position), other.parent_positions.end())
                    << domain << "/" << instance << ": " << to_string(other) << " was derived through the initially-true "
                    << to_string(records[position]);
            }
            if (records[position].fact_atom_index.has_value())
            {
                EXPECT_TRUE(landmarks->get_predecessors(*records[position].fact_atom_index).empty()) << domain << "/" << instance;
            }
        }
    }
    EXPECT_GT(total_initially_true, 0u) << "no shipped instance had an initially-true landmark, so nothing was tested";
}

/// @brief F3: the stop is over members, not over the pattern.
///
/// `landmark_lifted_static` with the filter off is the witness: `ready(?)` has the single member
/// `ready(t1)` (the `achieve-slow` completions are statically inconsistent), so `ready(t2)` being
/// true in `I` matches the *pattern* but is no way to satisfy the recorded landmark.
TEST(MimirTests, SearchLandmarksLiftedInitiallyTrueIsOverMembersTest)
{
    const auto problem = parse("landmark_lifted_static", "test_problem_pattern_only.pddl");

    auto options = LiftedFactLandmarkGeneratorOptions {};
    options.use_static_filter = false;
    const auto landmarks = LiftedFactLandmarkGenerator::create(problem, options);

    const auto ready_t1 = find_lifted(landmarks, "ready(t1)");
    ASSERT_NE(ready_t1, nullptr) << "the promoted member landmark must exist";
    EXPECT_FALSE(ready_t1->initially_true) << "ready(t2) in I matches the pattern but is not a member";

    // ... and because it was not stopped, the chain below it exists.
    EXPECT_NE(find_landmark(landmarks, "ready", { "t1" }), nullptr);
}

/**
 * T8: the graph contract.
 */

TEST(MimirTests, SearchLandmarksLiftedGraphContractTest)
{
    const auto problem = parse("gripper");
    const auto landmarks = LiftedFactLandmarkGenerator::create(problem);

    EXPECT_FALSE(landmarks->has_achiever_index());

    const auto some_landmark = landmarks->get_landmark_atom_indices().front();
    EXPECT_THROW((void) landmarks->get_achiever_action_indices(some_landmark), std::logic_error);
    EXPECT_THROW((void) landmarks->get_achievers(some_landmark), std::logic_error);
    EXPECT_THROW((void) landmarks->get_first_achiever_action_indices(some_landmark), std::logic_error);
    EXPECT_THROW((void) landmarks->get_first_achievers(some_landmark), std::logic_error);
    EXPECT_THROW((void) landmarks->get_unique_achiever_action_index(some_landmark), std::logic_error);
    EXPECT_THROW((void) landmarks->get_unique_achiever(some_landmark), std::logic_error);
    EXPECT_THROW((void) LandmarkTransitionOrderingStrategy(landmarks), std::logic_error);

    const auto grounder = LiftedGrounder(problem);
    const auto ground_actions = grounder.create_ground_actions();
    ASSERT_FALSE(ground_actions.empty());
    EXPECT_THROW((void) landmarks->is_landmark_achiever(ground_actions.front()), std::logic_error);
    EXPECT_THROW((void) landmarks->is_first_landmark_achiever(ground_actions.front()), std::logic_error);
    EXPECT_THROW((void) landmarks->is_unique_landmark_achiever(ground_actions.front()), std::logic_error);
    EXPECT_THROW((void) landmarks->get_landmarks_achieved_by_action(ground_actions.front()), std::logic_error);
    EXPECT_THROW((void) landmarks->get_landmarks_uniquely_achieved_by_action(ground_actions.front()), std::logic_error);

    // The grounded generator keeps the index, so nothing above changes for it.
    const auto grounded = ApproximateFactLandmarkGenerator::create(grounder);
    EXPECT_TRUE(grounded->has_achiever_index());
    EXPECT_NO_THROW((void) grounded->get_achiever_action_indices(grounded->get_landmark_atom_indices().front()));
    EXPECT_NO_THROW((void) LandmarkTransitionOrderingStrategy(grounded));
    EXPECT_TRUE(grounded->get_lifted_landmarks().empty());

    EXPECT_EQ(landmarks->get_landmark_atoms().size(), landmarks->get_landmark_atom_indices().size());

    auto seen = std::set<IndexList> {};
    auto union_of_members = IndexList {};
    for (const auto& members : landmarks->get_disjunctive_landmarks())
    {
        EXPECT_FALSE(members.empty());
        EXPECT_TRUE(std::is_sorted(members.begin(), members.end()));
        EXPECT_FALSE(has_duplicates(members));
        EXPECT_TRUE(seen.insert(members).second) << "duplicate disjunctive landmark";
        for (const auto member : members)
        {
            EXPECT_FALSE(landmarks->is_landmark(member));
        }
        union_of_members.insert(union_of_members.end(), members.begin(), members.end());
    }
    std::sort(union_of_members.begin(), union_of_members.end());
    union_of_members.erase(std::unique(union_of_members.begin(), union_of_members.end()), union_of_members.end());
    EXPECT_EQ(landmarks->get_disjunctive_landmark_atom_indices(), union_of_members);

    for (const auto atom_index : landmarks->get_landmark_atom_indices())
    {
        EXPECT_FALSE(has_duplicates(landmarks->get_predecessors(atom_index)));
        EXPECT_FALSE(has_duplicates(landmarks->get_successors(atom_index)));
        for (const auto predecessor_index : landmarks->get_predecessors(atom_index))
        {
            EXPECT_TRUE(landmarks->is_landmark(predecessor_index));
            const auto& successors = landmarks->get_successors(predecessor_index);
            EXPECT_NE(std::find(successors.begin(), successors.end(), atom_index), successors.end());
        }
        for (const auto successor_index : landmarks->get_successors(atom_index))
        {
            EXPECT_TRUE(landmarks->is_landmark(successor_index));
            const auto& predecessors = landmarks->get_predecessors(successor_index);
            EXPECT_NE(std::find(predecessors.begin(), predecessors.end(), atom_index), predecessors.end());
        }
    }

    // A lifted problem interns atoms during search, so an atom index far beyond the graph's range
    // is a legitimate question with the answer "no edges", not an out-of-bounds read.
    const auto beyond = Index(num_fluent_atoms(problem) + 100000);
    EXPECT_TRUE(landmarks->get_predecessors(beyond).empty());
    EXPECT_TRUE(landmarks->get_successors(beyond).empty());
    EXPECT_FALSE(landmarks->is_landmark(beyond));

    /* Promotion turns a one-member set into a fact landmark that then stays out of every set.
       `landmark_lifted_static` without the static filter is the minimal witness: `ready(?)` is
       partial (the two achievers disagree on the position) yet only `achieve-fast` contributes a
       statically consistent instance, so the set has exactly one member. */
    const auto promotion_problem = parse("landmark_lifted_static");
    auto promotion_options = LiftedFactLandmarkGeneratorOptions {};
    promotion_options.use_static_filter = false;

    const auto promoted = LiftedFactLandmarkGenerator::create(promotion_problem, promotion_options);
    const auto ready = find_landmark(promoted, "ready", { "t1" });
    ASSERT_NE(ready, nullptr);
    for (const auto& members : promoted->get_disjunctive_landmarks())
    {
        EXPECT_EQ(std::find(members.begin(), members.end(), ready->get_index()), members.end());
    }

    // The partial form is gone: it is dropped, not kept alongside the fact it promoted to.
    EXPECT_EQ(find_lifted(promoted, "ready(?)"), nullptr);
    EXPECT_TRUE(sets_over_predicate(promoted, "ready").empty());
    const auto ready_record = find_lifted(promoted, "ready(t1)");
    ASSERT_NE(ready_record, nullptr);
    EXPECT_TRUE(ready_record->fact_atom_index.has_value());
}

/**
 * §9.6: with every reachability option off, the generator must be byte-identical to what pymimir
 * 0.16.0 released (`e3acfad18`).
 *
 * This is the regression test for every option §9 adds, and it is worth a golden file rather than a
 * property: each option below narrows what the extractor keeps, and "narrowed something it should
 * not have" is invisible to any assertion that does not know the previous answer. The goldens were
 * generated from `e3acfad18` and are regenerated with
 * `MIMIR_WRITE_LANDMARK_GOLDEN=1 ./search_landmarks_lifted_fact_landmarks_test --gtest_filter=*AllOptionsOff*`
 * -- which should only ever be run when a change to the *released* behaviour is intended.
 */

/// @brief Every option off: the pre-0.17 generator exactly.
LiftedFactLandmarkGeneratorOptions all_reachability_options_off()
{
    auto options = LiftedFactLandmarkGeneratorOptions {};
    options.reachability_filter_members = false;
    options.reachability_disambiguation = ReachabilityDisambiguation::OFF;
    options.first_achievers_restricted = false;
    options.verify_pi_plus = false;
    options.complete_fact_landmarks = CompleteFactLandmarks::OFF;
    return options;
}

namespace
{

/// @brief `(directory, problem file, golden name)` for every tracked instance the harness covers.
const std::vector<std::tuple<std::string, std::string, std::string>>& golden_instances()
{
    static const auto instances = []
    {
        auto result = std::vector<std::tuple<std::string, std::string, std::string>> {
            { "blocks_4", "test_problem.pddl", "blocks_4" },
            { "gripper", "test_problem.pddl", "gripper" },
            { "delivery", "test_problem.pddl", "delivery" },
            { "childsnack", "test_problem.pddl", "childsnack" },
            { "ferry", "test_problem.pddl", "ferry" },
            { "miconic", "test_problem.pddl", "miconic" },
            { "logistics", "test_problem.pddl", "logistics" },
            { "spanner", "p30-hard.pddl", "spanner_p30_hard" },
            { "landmark_cond_effect_dedup", "test_problem.pddl", "landmark_cond_effect_dedup" },
            { "landmark_lifted_static", "test_problem.pddl", "landmark_lifted_static" },
            { "landmark_lifted_occurrences", "test_problem.pddl", "landmark_lifted_occurrences" },
            { "landmark_lifted_cond_effect", "test_problem.pddl", "landmark_lifted_cond_effect" },
            { "landmark_lifted_unreachable_members", "test_problem.pddl", "landmark_lifted_unreachable_members" },
            { "landmark_lifted_sdp_gate", "test_problem.pddl", "landmark_lifted_sdp_gate" },
            { "landmark_lifted_sdp_gate", "test_problem_closed.pddl", "landmark_lifted_sdp_gate_closed" },
            { "landmark_ipc_smallest/miconic-ipc-hard", "p30-hard.pddl", "miconic_ipc_p30_hard" },
        };
        for (const auto& domain : ipc_domains())
        {
            result.emplace_back("landmark_ipc_smallest/" + domain, "p69.pddl", domain + "_p69");
        }
        return result;
    }();
    return instances;
}

fs::path golden_path(const std::string& name) { return fs::path(std::string(DATA_DIR) + "../tests/unit/search/landmarks/golden/" + name + ".txt"); }

}

TEST(MimirTests, SearchLandmarksLiftedAllOptionsOffMatchesTheReleasedOutputTest)
{
    const auto write_goldens = (std::getenv("MIMIR_WRITE_LANDMARK_GOLDEN") != nullptr);

    for (const auto& [domain, instance, name] : golden_instances())
    {
        const auto rendered = render_graph(LiftedFactLandmarkGenerator::create(parse(domain, instance), all_reachability_options_off()));
        const auto path = golden_path(name);

        if (write_goldens)
        {
            auto out = std::ofstream(path);
            ASSERT_TRUE(out.good()) << path;
            out << rendered;
            continue;
        }

        auto in = std::ifstream(path);
        ASSERT_TRUE(in.good()) << "missing golden " << path << " -- regenerate with MIMIR_WRITE_LANDMARK_GOLDEN=1";
        const auto expected = std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        EXPECT_EQ(rendered, expected) << domain << "/" << instance << " no longer matches the 0.16.0 output";
    }
}

/**
 * §2.5 reachability. An atom no schema can produce is in no reachable state, so it is not one of
 * the members a plan could have made true and dropping it leaves the landmark property intact.
 *
 * Without this the lifted rule instantiates a partial landmark over the *typed* universe, which on
 * the domains below is not a precision nicety but a difference of two orders of magnitude.
 */

TEST(MimirTests, SearchLandmarksLiftedUnaddablePredicateTest)
{
    /* miconic's `origin` is FLUENT because `board` deletes it, and no schema adds it. Every
       non-initial instance is therefore unreachable by construction, and `origin(?, ?)` over 485
       passengers x 196 floors was 95,060 members of which 485 could ever hold. */
    const auto problem = parse("landmark_ipc_smallest/miconic-ipc-hard", "p30-hard.pddl");

    auto initial_origin = std::set<std::string> {};
    for (const auto atom : problem->get_fluent_initial_atoms())
    {
        if (atom->get_predicate()->get_name() == "origin")
        {
            initial_origin.insert(atom_signature(atom));
        }
    }
    ASSERT_FALSE(initial_origin.empty());

    // Ground first: the repository then holds exactly the delete-relaxed-reachable universe, and
    // what the extractor adds on top of it is the thing being bounded.
    const auto grounder = LiftedGrounder(problem);
    const auto ground_actions = grounder.create_ground_actions();
    ASSERT_FALSE(ground_actions.empty());
    const auto atoms_after_grounding = num_fluent_atoms(problem);

    const auto landmarks = LiftedFactLandmarkGenerator::create(problem);

    // No `origin` set survives, and any `origin` landmark that does is one of the initial atoms.
    EXPECT_TRUE(sets_over_predicate(landmarks, "origin").empty());
    for (const auto& record : landmarks->get_lifted_landmarks())
    {
        if (record.predicate->get_name() != "origin")
        {
            continue;
        }
        for (const auto member : record.member_atom_indices)
        {
            EXPECT_TRUE(initial_origin.count(atom_signature(resolve_atom(landmarks, member))) > 0)
                << to_string(record) << " keeps the unreachable member " << atom_signature(resolve_atom(landmarks, member));
        }
        // Every member is true at I, so the record cannot have been expanded either.
        EXPECT_TRUE(record.initially_true) << to_string(record);
    }

    /* The bound that says the vocabulary is a planning one rather than a typed one. Before the
       filter this instance interned 96,226 atoms against the grounder's 1,651. */
    const auto atoms_after_extraction = num_fluent_atoms(problem);
    EXPECT_LE(atoms_after_extraction, atoms_after_grounding + 16)
        << "interned " << (atoms_after_extraction - atoms_after_grounding) << " atoms beyond the grounder's universe of "
        << atoms_after_grounding;
}

TEST(MimirTests, SearchLandmarksLiftedTypeGatedAdderTest)
{
    /* spanner's `at` IS added -- by `walk`, whose only `at` effect binds a `?m - man`. So
       `at(spanner1, l)` and `at(nut1, l)` are unreachable for exactly the same reason, caught by
       the type check inside the unification rather than by "no adder at all". */
    const auto problem = parse("spanner", "p30-hard.pddl");
    const auto landmarks = LiftedFactLandmarkGenerator::create(problem);

    const auto is_a = [](Object object, const std::string& type_name)
    {
        const auto& bases = object->get_bases();
        return std::any_of(bases.begin(), bases.end(), [&](Type type) { return type->get_name() == type_name; });
    };

    /* Asked of every `at` atom the graph names, fact or member. §2.7 turned spanner's `at` sets
       into fact landmarks, so a members-only check would now pass vacuously -- and the claim was
       never about sets: an `at` naming a spanner or a nut is admissible only where the instance
       put it, because nothing can move one. */
    auto initial = std::set<std::string> {};
    for (const auto atom : problem->get_fluent_initial_atoms())
    {
        initial.insert(atom_signature(atom));
    }

    auto atom_indices = landmarks->get_landmark_atom_indices();
    for (const auto& members : landmarks->get_disjunctive_landmarks())
    {
        atom_indices.insert(atom_indices.end(), members.begin(), members.end());
    }

    auto num_at_atoms = size_t(0);
    for (const auto atom_index : atom_indices)
    {
        const auto atom = resolve_atom(landmarks, atom_index);
        if (atom->get_predicate()->get_name() != "at")
        {
            continue;
        }
        ++num_at_atoms;
        const auto located = atom->get_objects().front();
        if (is_a(located, "spanner") || is_a(located, "nut"))
        {
            EXPECT_TRUE(initial.count(atom_signature(atom)) > 0)
                << "unreachable: nothing moves a spanner or a nut, so " << atom_signature(atom) << " can only hold where I put it";
        }
    }
    EXPECT_GT(num_at_atoms, 0u) << "no `at` atom at all: the test would pass vacuously";
}

TEST(MimirTests, SearchLandmarksLiftedStaticallyGatedAdderTest)
{
    // The general case: `marked` is added only under a static condition `t3` fails.
    const auto problem = parse("landmark_lifted_unreachable_members");
    const auto landmarks = LiftedFactLandmarkGenerator::create(problem);

    const auto marked_sets = sets_over_predicate(landmarks, "marked");
    ASSERT_EQ(marked_sets.size(), 1u);
    EXPECT_EQ(marked_sets.front().size(), 2u);
    EXPECT_TRUE(contains_atom(landmarks, marked_sets.front(), "marked", { "t1" }));
    EXPECT_TRUE(contains_atom(landmarks, marked_sets.front(), "marked", { "t2" }));
    EXPECT_FALSE(contains_atom(landmarks, marked_sets.front(), "marked", { "t3" }));

    // The control: `want` adds `need` for anything, so nothing is filtered there.
    const auto need_sets = sets_over_predicate(landmarks, "need");
    ASSERT_EQ(need_sets.size(), 1u);
    EXPECT_EQ(need_sets.front().size(), 3u);
    EXPECT_TRUE(contains_atom(landmarks, need_sets.front(), "need", { "t3" }));
}

/**
 * §2.7, the self-dependent-precondition rule. Four domains where the plain intersection over all
 * achievers is empty and a landmark is waiting behind an achiever that cannot possibly be first.
 */

TEST(MimirTests, SearchLandmarksLiftedSelfDependentPreconditionFerryTest)
{
    // `on(car)` is achieved only by `board(car, ?loc)`, whose `at(car, ?loc)` has one adder,
    // `debark`, which needs `on(car)` itself -- so the car boards where the instance put it.
    const auto problem = parse("ferry");
    const auto landmarks = LiftedFactLandmarkGenerator::create(problem);

    const auto on_car1 = find_landmark(landmarks, "on", { "car1" });
    ASSERT_NE(on_car1, nullptr);
    const auto& predecessors = landmarks->get_predecessors(on_car1->get_index());
    EXPECT_TRUE(contains_atom(landmarks, predecessors, "at-ferry", { "loc3" }));  // car1's initial location
    EXPECT_TRUE(contains_atom(landmarks, predecessors, "empty-ferry", {}));
    EXPECT_TRUE(contains_atom(landmarks, predecessors, "at", { "car1", "loc3" }));
    EXPECT_TRUE(sets_over_predicate(landmarks, "at-ferry").empty()) << "the ferry's location is fixed, not a disjunction";
}

TEST(MimirTests, SearchLandmarksLiftedSelfDependentPreconditionMiconicTest)
{
    // The vacuous case: nothing adds `origin`, so `origin(p, ?f)` must be initial and the floor the
    // lift has to visit for `boarded(p)` is fixed.
    const auto problem = parse("miconic");
    const auto landmarks = LiftedFactLandmarkGenerator::create(problem);

    const auto boarded_p0 = find_landmark(landmarks, "boarded", { "p0" });
    ASSERT_NE(boarded_p0, nullptr);
    const auto& predecessors = landmarks->get_predecessors(boarded_p0->get_index());
    EXPECT_TRUE(contains_atom(landmarks, predecessors, "lift-at", { "f0" }));  // p0's origin floor
    EXPECT_TRUE(contains_atom(landmarks, predecessors, "origin", { "p0", "f0" }));
    EXPECT_TRUE(sets_over_predicate(landmarks, "lift-at").empty());
}

TEST(MimirTests, SearchLandmarksLiftedSelfDependentPreconditionLogisticsTest)
{
    /* The partial case. `in(p0, ?)` is loaded at `at(p0, ?loc)`, whose only surviving adders are
       the two unload actions -- `drive-truck`/`fly-airplane` cannot add `at(p0, ?)` because p0 is
       not a truck or an airplane -- and both need `in(p0, ?)`. So the load happens at p0's initial
       location, and the vehicle stays a disjunction over whatever can be there. */
    const auto problem = parse("logistics");
    const auto landmarks = LiftedFactLandmarkGenerator::create(problem);

    const auto in_p0 = find_lifted(landmarks, "in(p0, ?)");
    ASSERT_NE(in_p0, nullptr);
    const auto& records = landmarks->get_lifted_landmarks();

    auto derived = std::set<std::string> {};
    const auto in_p0_position = Index(in_p0 - records.data());
    for (const auto& record : records)
    {
        if (std::find(record.parent_positions.begin(), record.parent_positions.end(), in_p0_position) != record.parent_positions.end())
        {
            derived.insert(to_string(record));
        }
    }
    EXPECT_TRUE(derived.count("at(?, l0-0)") > 0) << "expected the vehicle to stay a disjunction over l0-0";
    EXPECT_TRUE(derived.count("at(p0, l0-0)") > 0) << "expected the package's own initial location";

    const auto vehicles = find_lifted(landmarks, "at(?, l0-0)");
    ASSERT_NE(vehicles, nullptr);
    EXPECT_GT(vehicles->member_atom_indices.size(), 1u);
}

TEST(MimirTests, SearchLandmarksLiftedSelfDependentPreconditionGateTest)
{
    /* §2.7 concludes "the instance the first achiever used was already there", which needs that no
       instance of the *pattern* was there. §2.1's member-level stop is not enough: an instance that
       is not a member leaves the landmark perfectly unsatisfied at `I` and still breaks the
       argument. The fixture puts exactly such an atom in `I` -- `held(t2)`, excluded from the
       members because t2 is not `usable` -- and the only difference between the two problems is
       whether it is there. */
    const auto open_gate = LiftedFactLandmarkGenerator::create(parse("landmark_lifted_sdp_gate"));
    const auto closed_gate = LiftedFactLandmarkGenerator::create(parse("landmark_lifted_sdp_gate", "test_problem_closed.pddl"));

    // Same landmark either way: `held(?)` over the two usable things.
    for (const auto& landmarks : { open_gate, closed_gate })
    {
        const auto held = find_lifted(landmarks, "held(?)");
        ASSERT_NE(held, nullptr);
        EXPECT_EQ(held->member_atom_indices.size(), 2u);
        EXPECT_TRUE(contains_atom(landmarks, held->member_atom_indices, "held", { "t1" }));
        EXPECT_TRUE(contains_atom(landmarks, held->member_atom_indices, "held", { "t3" }));
    }

    /* With the gate open the rule fires: every adder of `ready` is a `prep`, which needs `held`
       itself, so the `ready` instance the first `grab` used is initial and `?x` narrows to the
       things that start ready. */
    const auto narrowed = find_lifted(open_gate, "ready(?)");
    ASSERT_NE(narrowed, nullptr);
    EXPECT_EQ(narrowed->member_atom_indices.size(), 2u);
    EXPECT_FALSE(contains_atom(open_gate, narrowed->member_atom_indices, "ready", { "t2" }));

    // With `held(t2)` in `I` the gate closes and the expansion is the plain one.
    const auto plain = find_lifted(closed_gate, "ready(?)");
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(plain->member_atom_indices.size(), 3u);
    EXPECT_TRUE(contains_atom(closed_gate, plain->member_atom_indices, "ready", { "t2" }));
}

/**
 * Determinism. The extraction rule is order-sensitive by construction -- §2.4 subsumption keeps a
 * general pattern only when no more specific one was inserted before it -- so an unspecified
 * iteration order would make the member vocabulary a property of the standard library the binary
 * was built against. Two runs must agree, and the order must be the one the generator declares.
 */

TEST(MimirTests, SearchLandmarksLiftedDeterministicOrderTest)
{
    auto instances = std::vector<std::pair<std::string, std::string>> { { "blocks_4", "test_problem.pddl" },
                                                                       { "gripper", "test_problem.pddl" },
                                                                       { "childsnack", "test_problem.pddl" },
                                                                       { "landmark_lifted_occurrences", "test_problem.pddl" } };
    for (const auto& domain : ipc_domains())
    {
        instances.emplace_back("landmark_ipc_smallest/" + domain, "p69.pddl");
    }

    for (const auto& [domain, instance] : instances)
    {
        // Two independent parses, so repository index assignment is exercised too, not just the
        // second call over a warm problem.
        const auto first = render_graph(LiftedFactLandmarkGenerator::create(parse(domain, instance)));
        const auto second = render_graph(LiftedFactLandmarkGenerator::create(parse(domain, instance)));
        EXPECT_EQ(first, second) << domain << "/" << instance;

        const auto problem = parse(domain, instance);
        const auto landmarks = LiftedFactLandmarkGenerator::create(problem);
        EXPECT_EQ(render_graph(LiftedFactLandmarkGenerator::create(problem)), render_graph(landmarks)) << domain << "/" << instance;

        /* The declared order: goal atoms first in goal order, then one block of records per
           expansion, and within a block ascending by the derived predicate's index. A record's
           first parent is the one that created it, so a block is a maximal run of consecutive
           records sharing that first parent. */
        const auto& records = landmarks->get_lifted_landmarks();
        for (size_t i = 1; i < records.size(); ++i)
        {
            if (records[i - 1].parent_positions.empty() || records[i].parent_positions.empty()
                || records[i - 1].parent_positions.front() != records[i].parent_positions.front())
            {
                continue;  // a block boundary
            }
            EXPECT_LE(records[i - 1].predicate->get_index(), records[i].predicate->get_index())
                << domain << "/" << instance << ": " << to_string(records[i - 1]) << " before " << to_string(records[i]);
        }
    }
}

/**
 * T9: the soundness oracle. Mandatory for every lifted landmark; recorded, not asserted, for the
 * grounded generator.
 */

TEST(MimirTests, SearchLandmarksLiftedSoundnessOracleTest)
{
    auto instances = std::vector<std::pair<std::string, std::string>> {
        { "blocks_4", "test_problem.pddl" }, { "gripper", "test_problem.pddl" }, { "delivery", "test_problem.pddl" }, { "childsnack", "test_problem.pddl" }
    };
    for (const auto& domain : ipc_domains())
    {
        instances.emplace_back("landmark_ipc_smallest/" + domain, "p69.pddl");
    }

    auto grounded_totals = OracleResult {};
    for (const auto& [domain, instance] : instances)
    {
        const auto problem = parse(domain, instance);
        ASSERT_TRUE(problem->get_problem_and_domain_axioms().empty()) << domain << ": the oracle does not model axioms";

        const auto grounder = LiftedGrounder(problem);
        const auto lifted = LiftedFactLandmarkGenerator::create(problem);

        auto grounded_options = FactLandmarkGeneratorOptions {};
        grounded_options.max_disjunctive_landmark_size = size_t(1) << 30;  // uncapped, the §6 "G" configuration
        const auto grounded = ApproximateFactLandmarkGenerator::create(grounder, grounded_options);

        /* Built after both generators ran, so the relaxed task covers every atom they interned. */
        const auto task = RelaxedTask(problem, grounder.create_ground_actions());
        ASSERT_TRUE(task.goal_reachable_without({})) << domain << "/" << instance << ": unsolvable even relaxed, the oracle would be vacuous";

        const auto lifted_result = run_oracle(lifted, task, domain + "/" + instance + " [lifted]", true);
        const auto grounded_result = run_oracle(grounded, task, domain + "/" + instance + " [grounded]", false);
        grounded_totals.num_tested += grounded_result.num_tested;
        grounded_totals.num_failed += grounded_result.num_failed;

        std::cout << "oracle " << domain << "/" << instance << ": lifted " << lifted_result.num_failed << "/" << lifted_result.num_tested
                  << " refuted, grounded " << grounded_result.num_failed << "/" << grounded_result.num_tested << " refuted\n";
    }

    std::cout << "oracle TOTAL grounded: " << grounded_totals.num_failed << "/" << grounded_totals.num_tested << " refuted\n";
}

/**
 * T10: end to end under a lifted parse, which is what this generator exists for.
 */

TEST(MimirTests, SearchLandmarksLiftedEndToEndTest)
{
    const auto instances =
        std::vector<std::pair<std::string, std::string>> { { "gripper", "test_problem.pddl" }, { "childsnack", "test_problem.pddl" } };

    auto any_instance_grew = false;
    for (const auto& [domain, instance] : instances)
    {
        const auto problem = parse(domain, instance);
        const auto landmarks = LiftedFactLandmarkGenerator::create(problem);
        const auto atoms_at_build_time = num_fluent_atoms(problem);

        const auto context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));

        auto options = iw::Options {};
        options.max_arity = 1;
        options.max_num_states = 500000;
        options.landmark_novelty_graph = landmarks;
        options.landmark_novelty_disjunctive = true;
        options.landmark_novelty_all_private = true;

        const auto result = iw::find_solution(context, options);
        EXPECT_EQ(result.status, SearchStatus::SOLVED) << domain << "/" << instance;
        ASSERT_TRUE(result.plan.has_value()) << domain << "/" << instance;
        EXPECT_GT(result.plan->get_length(), 0u) << domain << "/" << instance;

        /* A lifted parse interns atoms as the search reaches them, so the graph's per-atom vectors
           are outgrown by the very search that reads them. Asking about the newest atom index must
           answer "no edges" rather than read out of bounds -- the resize path T8 pins. */
        const auto atoms_after_search = num_fluent_atoms(problem);
        EXPECT_GE(atoms_after_search, atoms_at_build_time) << domain << "/" << instance;
        any_instance_grew |= (atoms_after_search > atoms_at_build_time);

        const auto newest_atom = Index(atoms_after_search - 1);
        EXPECT_NO_THROW((void) landmarks->get_predecessors(newest_atom));
        EXPECT_NO_THROW((void) landmarks->get_successors(newest_atom));
        EXPECT_NO_THROW((void) landmarks->is_landmark(newest_atom));
    }

    EXPECT_TRUE(any_instance_grew) << "no instance interned an atom the generator had not already named, so the resize path never ran";
}

}
