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

#include "mimir/search/relaxed_reachability.hpp"

#include "mimir/formalism/action.hpp"
#include "mimir/formalism/axiom.hpp"
#include "mimir/formalism/domain.hpp"
#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/ground_axiom.hpp"
#include "mimir/formalism/ground_conjunctive_condition.hpp"
#include "mimir/formalism/ground_effects.hpp"
#include "mimir/formalism/ground_literal.hpp"
#include "mimir/formalism/object.hpp"
#include "mimir/formalism/parser.hpp"
#include "mimir/formalism/predicate.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/grounders/lifted.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"
#include "mimir/search/landmarks/lifted_fact_landmark_generator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#ifndef _WIN32
#include <sys/resource.h>
#endif
#ifdef __APPLE__
#include <mach/mach.h>
#endif

using namespace mimir::search;
using namespace mimir::formalism;

namespace fs = std::filesystem;

namespace mimir::tests
{
namespace
{

/**
 * Parsing helpers
 */

Problem parse_fixture(const std::string& domain_name, const std::string& problem_filename)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + domain_name + "/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + domain_name + "/" + problem_filename);
    return ProblemImpl::create(domain_file, problem_file, loki::ParserOptions {});
}

std::vector<std::string> list_problem_files(const std::string& domain_name)
{
    auto result = std::vector<std::string> {};
    const auto directory = fs::path(std::string(DATA_DIR) + domain_name);
    if (!fs::exists(directory))
    {
        return result;
    }
    for (const auto& entry : fs::directory_iterator(directory))
    {
        if (entry.path().extension() != ".pddl" || entry.path().filename() == "domain.pddl")
        {
            continue;
        }
        result.push_back(entry.path().filename().string());
    }
    std::sort(result.begin(), result.end());
    return result;
}

/**
 * The ground-level reference implementation.
 *
 * `LiftedGrounder::create_ground_actions()` returns exactly the ground actions whose delete-free copy was
 * instantiated and whose static condition holds -- a superset of the truly relaxed-reachable ones, because
 * the delete-free exploration ignores negative static conditions. Closing the initial state under that
 * superset gives the exact ground delete-relaxed reachable atom set, which is what the engine must produce.
 * Grounding is far too expensive at test scale in production; inside a test it is the ground truth.
 */
class GroundReference
{
public:
    struct Rule
    {
        IndexList fluent_preconditions;
        IndexList derived_preconditions;
        IndexList fluent_adds;
        IndexList derived_adds;
    };

    explicit GroundReference(const Problem& problem) : m_problem(problem)
    {
        auto grounder = LiftedGrounder(problem);

        for (const auto action : grounder.create_ground_actions())
        {
            auto base_fluent = IndexList {};
            auto base_derived = IndexList {};
            for (const auto atom_index : action->get_conjunctive_condition()->get_precondition<PositiveTag, FluentTag>())
            {
                base_fluent.push_back(atom_index);
            }
            for (const auto atom_index : action->get_conjunctive_condition()->get_precondition<PositiveTag, DerivedTag>())
            {
                base_derived.push_back(atom_index);
            }
            for (const auto conditional_effect : action->get_conditional_effects())
            {
                auto rule = Rule { base_fluent, base_derived, {}, {} };
                for (const auto atom_index : conditional_effect->get_conjunctive_condition()->get_precondition<PositiveTag, FluentTag>())
                {
                    rule.fluent_preconditions.push_back(atom_index);
                }
                for (const auto atom_index : conditional_effect->get_conjunctive_condition()->get_precondition<PositiveTag, DerivedTag>())
                {
                    rule.derived_preconditions.push_back(atom_index);
                }
                for (const auto atom_index : conditional_effect->get_conjunctive_effect()->get_propositional_effects<PositiveTag>())
                {
                    rule.fluent_adds.push_back(atom_index);
                }
                if (rule.fluent_adds.empty())
                {
                    continue;
                }
                m_rules.push_back(std::move(rule));
            }
        }

        for (const auto axiom : grounder.create_ground_axioms())
        {
            auto rule = Rule {};
            for (const auto atom_index : axiom->get_conjunctive_condition()->get_precondition<PositiveTag, FluentTag>())
            {
                rule.fluent_preconditions.push_back(atom_index);
            }
            for (const auto atom_index : axiom->get_conjunctive_condition()->get_precondition<PositiveTag, DerivedTag>())
            {
                rule.derived_preconditions.push_back(atom_index);
            }
            rule.derived_adds.push_back(axiom->get_literal()->get_atom()->get_index());
            m_rules.push_back(std::move(rule));
        }

        for (const auto atom : problem->get_fluent_initial_atoms())
        {
            m_initial_fluent_atoms.push_back(atom->get_index());
        }
    }

    struct Result
    {
        std::vector<char> fluent;
        std::vector<char> derived;
    };

    /// @brief Close the initial state under the rules. `forbidden_fluent` names atoms that are removed from
    /// the initial state and dropped from every add list -- exactly the engine's restricted-query semantics.
    Result close(const std::unordered_set<Index>& forbidden_fluent) const
    {
        auto num_fluent = size_t(0);
        auto num_derived = size_t(0);
        for (const auto& rule : m_rules)
        {
            for (const auto atom_index : rule.fluent_preconditions)
            {
                num_fluent = std::max(num_fluent, size_t(atom_index) + 1);
            }
            for (const auto atom_index : rule.fluent_adds)
            {
                num_fluent = std::max(num_fluent, size_t(atom_index) + 1);
            }
            for (const auto atom_index : rule.derived_preconditions)
            {
                num_derived = std::max(num_derived, size_t(atom_index) + 1);
            }
            for (const auto atom_index : rule.derived_adds)
            {
                num_derived = std::max(num_derived, size_t(atom_index) + 1);
            }
        }
        for (const auto atom_index : m_initial_fluent_atoms)
        {
            num_fluent = std::max(num_fluent, size_t(atom_index) + 1);
        }

        auto result = Result { std::vector<char>(num_fluent, 0), std::vector<char>(num_derived, 0) };
        for (const auto atom_index : m_initial_fluent_atoms)
        {
            if (forbidden_fluent.count(atom_index) == 0)
            {
                result.fluent[atom_index] = 1;
            }
        }

        auto changed = true;
        while (changed)
        {
            changed = false;
            for (const auto& rule : m_rules)
            {
                const auto holds = std::all_of(rule.fluent_preconditions.begin(),
                                               rule.fluent_preconditions.end(),
                                               [&](Index atom_index) { return result.fluent[atom_index]; })
                                   && std::all_of(rule.derived_preconditions.begin(),
                                                  rule.derived_preconditions.end(),
                                                  [&](Index atom_index) { return result.derived[atom_index]; });
                if (!holds)
                {
                    continue;
                }
                for (const auto atom_index : rule.fluent_adds)
                {
                    if (!result.fluent[atom_index] && forbidden_fluent.count(atom_index) == 0)
                    {
                        result.fluent[atom_index] = 1;
                        changed = true;
                    }
                }
                for (const auto atom_index : rule.derived_adds)
                {
                    if (!result.derived[atom_index])
                    {
                        result.derived[atom_index] = 1;
                        changed = true;
                    }
                }
            }
        }
        return result;
    }

    bool goal_reachable_without(const std::unordered_set<Index>& forbidden_fluent) const
    {
        const auto reached = close(forbidden_fluent);
        for (const auto atom_index : m_problem->get_goal_condition()->get_precondition<PositiveTag, FluentTag>())
        {
            if (atom_index >= reached.fluent.size() || !reached.fluent[atom_index])
            {
                return false;
            }
        }
        for (const auto atom_index : m_problem->get_goal_condition()->get_precondition<PositiveTag, DerivedTag>())
        {
            if (atom_index >= reached.derived.size() || !reached.derived[atom_index])
            {
                return false;
            }
        }
        return m_problem->static_goal_holds();
    }

    /// @brief The action-level oracle of Hoffmann-Porteous-Sebastia: every action that adds a member of
    /// `forbidden` is removed wholesale, side effects included. Strictly stronger than dropping the atom,
    /// so `goal_reachable_without_achievers` implies `goal_reachable_without`.
    bool goal_reachable_without_achievers(const std::unordered_set<Index>& forbidden_fluent) const
    {
        auto num_fluent = size_t(0);
        auto num_derived = size_t(0);
        for (const auto& rule : m_rules)
        {
            for (const auto atom_index : rule.fluent_preconditions)
                num_fluent = std::max(num_fluent, size_t(atom_index) + 1);
            for (const auto atom_index : rule.fluent_adds)
                num_fluent = std::max(num_fluent, size_t(atom_index) + 1);
            for (const auto atom_index : rule.derived_preconditions)
                num_derived = std::max(num_derived, size_t(atom_index) + 1);
            for (const auto atom_index : rule.derived_adds)
                num_derived = std::max(num_derived, size_t(atom_index) + 1);
        }
        for (const auto atom_index : m_initial_fluent_atoms)
            num_fluent = std::max(num_fluent, size_t(atom_index) + 1);

        auto fluent = std::vector<char>(num_fluent, 0);
        auto derived = std::vector<char>(num_derived, 0);
        for (const auto atom_index : m_initial_fluent_atoms)
        {
            if (forbidden_fluent.count(atom_index) == 0)
            {
                fluent[atom_index] = 1;
            }
        }

        auto changed = true;
        while (changed)
        {
            changed = false;
            for (const auto& rule : m_rules)
            {
                if (std::any_of(rule.fluent_adds.begin(), rule.fluent_adds.end(), [&](Index a) { return forbidden_fluent.count(a) > 0; }))
                {
                    continue;
                }
                const auto holds =
                    std::all_of(rule.fluent_preconditions.begin(), rule.fluent_preconditions.end(), [&](Index a) { return fluent[a]; })
                    && std::all_of(rule.derived_preconditions.begin(), rule.derived_preconditions.end(), [&](Index a) { return derived[a]; });
                if (!holds)
                {
                    continue;
                }
                for (const auto atom_index : rule.fluent_adds)
                {
                    if (!fluent[atom_index])
                    {
                        fluent[atom_index] = 1;
                        changed = true;
                    }
                }
                for (const auto atom_index : rule.derived_adds)
                {
                    if (!derived[atom_index])
                    {
                        derived[atom_index] = 1;
                        changed = true;
                    }
                }
            }
        }

        for (const auto atom_index : m_problem->get_goal_condition()->get_precondition<PositiveTag, FluentTag>())
        {
            if (atom_index >= fluent.size() || !fluent[atom_index])
            {
                return false;
            }
        }
        for (const auto atom_index : m_problem->get_goal_condition()->get_precondition<PositiveTag, DerivedTag>())
        {
            if (atom_index >= derived.size() || !derived[atom_index])
            {
                return false;
            }
        }
        return m_problem->static_goal_holds();
    }

private:
    Problem m_problem;
    std::vector<Rule> m_rules;
    IndexList m_initial_fluent_atoms;
};

/**
 * Comparison helpers
 */

std::string render(Predicate<FluentTag> predicate, const ObjectList& objects)
{
    auto stream = std::ostringstream {};
    stream << predicate->get_name() << "(";
    for (size_t i = 0; i < objects.size(); ++i)
    {
        stream << (i ? ", " : "") << objects[i]->get_name();
    }
    stream << ")";
    return stream.str();
}

/// @brief The engine's reachable fluent atoms, rendered by name so they can be compared against the
/// ground-level reference without interning anything into the problem.
std::set<std::string> engine_fluent_atoms(const Problem& problem, const ReachabilityTable& table)
{
    auto result = std::set<std::string> {};
    auto objects = ObjectList {};
    for (const auto predicate : problem->get_domain()->get_predicates<FluentTag>())
    {
        const auto tuples = table.get_reachable_tuples(predicate);
        for (size_t i = 0; i < tuples.size(); ++i)
        {
            tuples.write(i, objects);
            result.insert(render(predicate, objects));
        }
    }
    return result;
}

std::set<std::string> engine_derived_atoms(const Problem& problem, const ReachabilityTable& table)
{
    auto result = std::set<std::string> {};
    auto objects = ObjectList {};
    for (const auto predicate : problem->get_problem_and_domain_derived_predicates())
    {
        const auto tuples = table.get_reachable_tuples(predicate);
        for (size_t i = 0; i < tuples.size(); ++i)
        {
            tuples.write(i, objects);
            auto stream = std::ostringstream {};
            stream << predicate->get_name() << "(";
            for (size_t j = 0; j < objects.size(); ++j)
            {
                stream << (j ? ", " : "") << objects[j]->get_name();
            }
            stream << ")";
            result.insert(stream.str());
        }
    }
    return result;
}

std::set<std::string> reference_fluent_atoms(const Problem& problem, const GroundReference::Result& reached)
{
    auto result = std::set<std::string> {};
    for (Index atom_index = 0; atom_index < reached.fluent.size(); ++atom_index)
    {
        if (!reached.fluent[atom_index])
        {
            continue;
        }
        const auto atom = problem->get_repositories().get_ground_atom<FluentTag>(atom_index);
        result.insert(render(atom->get_predicate(), atom->get_objects()));
    }
    return result;
}

std::set<std::string> reference_derived_atoms(const Problem& problem, const GroundReference::Result& reached)
{
    auto result = std::set<std::string> {};
    for (Index atom_index = 0; atom_index < reached.derived.size(); ++atom_index)
    {
        if (!reached.derived[atom_index])
        {
            continue;
        }
        const auto atom = problem->get_repositories().get_ground_atom<DerivedTag>(atom_index);
        auto stream = std::ostringstream {};
        stream << atom->get_predicate()->get_name() << "(";
        const auto& objects = atom->get_objects();
        for (size_t j = 0; j < objects.size(); ++j)
        {
            stream << (j ? ", " : "") << objects[j]->get_name();
        }
        stream << ")";
        result.insert(stream.str());
    }
    return result;
}

std::string render_difference(const std::set<std::string>& lhs, const std::set<std::string>& rhs, size_t limit = 8)
{
    auto only_lhs = std::vector<std::string> {};
    std::set_difference(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), std::back_inserter(only_lhs));
    auto stream = std::ostringstream {};
    stream << only_lhs.size() << " atoms";
    for (size_t i = 0; i < std::min(limit, only_lhs.size()); ++i)
    {
        stream << (i ? ", " : ": ") << only_lhs[i];
    }
    if (only_lhs.size() > limit)
    {
        stream << ", ...";
    }
    return stream.str();
}

/// @brief The process high-water mark. It only ever grows, so in a table of many instances in one process it
/// says what the largest one cost, not what each one cost; `current_resident_bytes` supplies the per-row delta
/// and `MIMIR_RELAXED_REACHABILITY_BENCH_ONLY` gives a clean single-instance peak.
size_t peak_resident_bytes()
{
#ifndef _WIN32
    auto usage = rusage {};
    getrusage(RUSAGE_SELF, &usage);
#ifdef __APPLE__
    return size_t(usage.ru_maxrss);  ///< macOS reports bytes
#else
    return size_t(usage.ru_maxrss) * 1024;  ///< Linux reports kilobytes
#endif
#else
    return 0;
#endif
}

size_t current_resident_bytes()
{
#ifdef __APPLE__
    auto info = mach_task_basic_info {};
    auto count = mach_msg_type_number_t(MACH_TASK_BASIC_INFO_COUNT);
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
    {
        return 0;
    }
    return size_t(info.resident_size);
#elif defined(__linux__)
    auto stream = std::ifstream("/proc/self/statm");
    auto total = size_t(0);
    auto resident = size_t(0);
    if (stream >> total >> resident)
    {
        return resident * size_t(sysconf(_SC_PAGESIZE));
    }
    return 0;
#else
    return 0;
#endif
}

/// @brief The instances the exactness test walks. Everything under `data/` whose grounding is affordable.
const std::vector<std::string>& exactness_domains()
{
    static const auto domains = std::vector<std::string> { "blocks_4",
                                                           "gripper",
                                                           "delivery",
                                                           "childsnack",
                                                           "miconic",
                                                           "spanner",
                                                           "logistics",
                                                           "rovers",
                                                           "satellite",
                                                           "sokoban",
                                                           "transport",
                                                           "ferry",
                                                           "barman",
                                                           "visitall",
                                                           "philosophers",
                                                           "grid",
                                                           "driverlog",
                                                           "zenotravel",
                                                           "tpp",
                                                           "hiking",
                                                           "landmark_cond_effect_dedup",
                                                           "landmark_cross_parent",
                                                           "landmark_transition_ordering",
                                                           "landmark_lifted_cond_effect",
                                                           "landmark_lifted_occurrences",
                                                           "landmark_lifted_static",
                                                           "landmark_lifted_unreachable_members",
                                                           "relaxed_reachability_chain",
                                                           "relaxed_reachability_axiom" };
    return domains;
}

}

/**
 * 1. Exactness against the ground-level delete-relaxed closure.
 */

TEST(MimirTests, SearchRelaxedReachabilityExactnessTest)
{
    auto num_instances = size_t(0);
    auto num_mismatches = size_t(0);

    for (const auto& domain_name : exactness_domains())
    {
        for (const auto& problem_filename : list_problem_files(domain_name))
        {
            const auto problem = parse_fixture(domain_name, problem_filename);
            const auto label = domain_name + "/" + problem_filename;

            const auto reachability = RelaxedReachability::create(problem);
            const auto engine_fluent = engine_fluent_atoms(problem, reachability->get_table());
            const auto engine_derived = engine_derived_atoms(problem, reachability->get_table());

            const auto reference = GroundReference(problem);
            const auto reached = reference.close({});
            const auto reference_fluent = reference_fluent_atoms(problem, reached);
            const auto reference_derived = reference_derived_atoms(problem, reached);

            ++num_instances;
            if (engine_fluent != reference_fluent || engine_derived != reference_derived)
            {
                ++num_mismatches;
                ADD_FAILURE() << label << ": engine and ground closure differ.\n"
                              << "  only in engine (fluent):    " << render_difference(engine_fluent, reference_fluent) << "\n"
                              << "  only in reference (fluent): " << render_difference(reference_fluent, engine_fluent) << "\n"
                              << "  only in engine (derived):    " << render_difference(engine_derived, reference_derived) << "\n"
                              << "  only in reference (derived): " << render_difference(reference_derived, engine_derived);
            }

            EXPECT_EQ(reachability->is_goal_reachable(), reference.goal_reachable_without({})) << label;
        }
    }

    std::cout << "[relaxed-reachability] exactness: " << num_instances << " instances, " << num_mismatches << " mismatches" << std::endl;
    EXPECT_GT(num_instances, 30u);
}

/**
 * 2. Restricted-query semantics.
 */

TEST(MimirTests, SearchRelaxedReachabilityRestrictedQueryTest)
{
    static const auto instances = std::vector<std::pair<std::string, std::string>> { { "blocks_4", "test_problem.pddl" },
                                                                                     { "gripper", "test_problem.pddl" },
                                                                                     { "delivery", "test_problem.pddl" },
                                                                                     { "childsnack", "test_problem.pddl" },
                                                                                     { "miconic", "test_problem.pddl" },
                                                                                     { "spanner", "test_problem.pddl" },
                                                                                     { "logistics", "test_problem.pddl" },
                                                                                     { "philosophers", "test_problem.pddl" } };

    auto num_queries = size_t(0);
    auto num_achiever_oracle_agreements = size_t(0);
    auto num_achiever_oracle_stricter = size_t(0);

    for (const auto& [domain_name, problem_filename] : instances)
    {
        const auto problem = parse_fixture(domain_name, problem_filename);
        const auto label = domain_name + "/" + problem_filename;

        const auto reachability = RelaxedReachability::create(problem);
        const auto reference = GroundReference(problem);

        /* Every landmark the lifted generator produces is a query. */
        const auto landmarks = landmarks::LiftedFactLandmarkGenerator::create(problem, {});
        auto queries = std::vector<GroundAtomList<FluentTag>> {};
        for (const auto atom_index : landmarks->get_landmark_atom_indices())
        {
            queries.push_back({ problem->get_repositories().get_ground_atom<FluentTag>(atom_index) });
        }
        for (const auto& members : landmarks->get_disjunctive_landmarks())
        {
            auto atoms = GroundAtomList<FluentTag> {};
            for (const auto atom_index : members)
            {
                atoms.push_back(problem->get_repositories().get_ground_atom<FluentTag>(atom_index));
            }
            queries.push_back(std::move(atoms));
        }
        /* Plus every reachable atom on its own, so the test is not limited to what the generator found. */
        for (const auto predicate : problem->get_domain()->get_predicates<FluentTag>())
        {
            const auto tuples = reachability->get_reachable_tuples(predicate);
            for (size_t i = 0; i < tuples.size(); ++i)
            {
                queries.push_back({ problem->get_or_create_ground_atom<FluentTag>(predicate, tuples[i]) });
            }
        }

        ASSERT_FALSE(queries.empty()) << label;

        for (const auto& atoms : queries)
        {
            auto forbidden = std::unordered_set<Index> {};
            for (const auto atom : atoms)
            {
                forbidden.insert(atom->get_index());
            }

            /* (a) The atom-level restriction has to match the ground-level closure atom for atom. */
            const auto table = reachability->compute_restricted(atoms);
            const auto reached = reference.close(forbidden);
            EXPECT_EQ(engine_fluent_atoms(problem, table), reference_fluent_atoms(problem, reached)) << label;
            EXPECT_EQ(engine_derived_atoms(problem, table), reference_derived_atoms(problem, reached)) << label;

            /* (b) The early-exit goal query has to agree with the same closure. */
            const auto engine_verdict = reachability->is_goal_reachable_without(atoms);
            EXPECT_EQ(engine_verdict, reference.goal_reachable_without(forbidden)) << label;
            EXPECT_EQ(engine_verdict, table.is_goal_reachable()) << label;

            /* (c) The Hoffmann-Porteous-Sebastia oracle removes whole achieving actions, so it can only ever
                   be stricter. A disagreement in the other direction would be an engine bug. */
            const auto achiever_verdict = reference.goal_reachable_without_achievers(forbidden);
            EXPECT_TRUE(!achiever_verdict || engine_verdict) << label << ": achiever oracle reached the goal where the atom-level query did not";
            num_achiever_oracle_agreements += (achiever_verdict == engine_verdict) ? 1 : 0;
            num_achiever_oracle_stricter += (achiever_verdict != engine_verdict) ? 1 : 0;
            ++num_queries;
        }
    }

    std::cout << "[relaxed-reachability] restricted queries: " << num_queries << " tested, achiever oracle agreed on "
              << num_achiever_oracle_agreements << " and was stricter on " << num_achiever_oracle_stricter << std::endl;
}

/**
 * 3. Conditional effects and axioms.
 */

TEST(MimirTests, SearchRelaxedReachabilityConditionalEffectTest)
{
    const auto problem = parse_fixture("relaxed_reachability_chain", "test_problem.pddl");
    const auto reachability = RelaxedReachability::create(problem);

    const auto& predicates = problem->get_domain()->get_name_to_predicate<FluentTag>();
    const auto object = [&](const std::string& name) { return problem->get_problem_or_domain_object(name); };

    /* `unlocked(g1)` is added only by the conditional branch of `open`, whose condition `charged(g1)` is in
       turn only produced by `charge`. A fixpoint that evaluated conditional effects as unconditional would
       reach `unlocked(g2)` as well; one that ignored them would reach neither. */
    EXPECT_TRUE(reachability->is_reachable(predicates.at("charged"), { object("g1") }));
    EXPECT_FALSE(reachability->is_reachable(predicates.at("charged"), { object("g2") }));
    EXPECT_TRUE(reachability->is_reachable(predicates.at("unlocked"), { object("g1") }));
    EXPECT_FALSE(reachability->is_reachable(predicates.at("unlocked"), { object("g2") }));
    EXPECT_TRUE(reachability->is_goal_reachable());

    /* Forbidding the conditional effect's own condition removes the conditional add with it. */
    const auto charged_g1 = problem->get_or_create_ground_atom<FluentTag>(predicates.at("charged"), { object("g1") });
    const auto table = reachability->compute_restricted(GroundAtomList<FluentTag> { charged_g1 });
    EXPECT_FALSE(table.is_reachable(predicates.at("unlocked"), { object("g1") }));
    EXPECT_FALSE(reachability->is_goal_reachable_without(GroundAtomList<FluentTag> { charged_g1 }));
}

TEST(MimirTests, SearchRelaxedReachabilityAxiomTest)
{
    const auto problem = parse_fixture("relaxed_reachability_axiom", "test_problem.pddl");
    const auto reachability = RelaxedReachability::create(problem);

    const auto object = [&](const std::string& name) { return problem->get_problem_or_domain_object(name); };
    const auto& fluent_predicates = problem->get_domain()->get_name_to_predicate<FluentTag>();
    const auto& derived_predicates = problem->get_domain()->get_name_to_predicate<DerivedTag>();

    /* `above` is the transitive closure of `on`; the axiom is a genuine recursive rule and the fixpoint has
       to iterate it. */
    EXPECT_TRUE(reachability->is_reachable(derived_predicates.at("above"), { object("b1"), object("b2") }));
    EXPECT_TRUE(reachability->is_reachable(derived_predicates.at("above"), { object("b1"), object("b3") }));
    EXPECT_TRUE(reachability->is_goal_reachable());

    /* Without `on(b1, b2)` the closure loses `above(b1, b3)` and the goal with it. */
    const auto on_b1_b2 = problem->get_or_create_ground_atom<FluentTag>(fluent_predicates.at("on"), { object("b1"), object("b2") });
    const auto table = reachability->compute_restricted(GroundAtomList<FluentTag> { on_b1_b2 });
    EXPECT_FALSE(table.is_reachable(derived_predicates.at("above"), { object("b1"), object("b3") }));
    EXPECT_FALSE(reachability->is_goal_reachable_without(GroundAtomList<FluentTag> { on_b1_b2 }));

    /* And the ground reference agrees, axioms included. */
    const auto reference = GroundReference(problem);
    EXPECT_EQ(engine_derived_atoms(problem, reachability->get_table()), reference_derived_atoms(problem, reference.close({})));
}

/**
 * 5. Determinism.
 */

TEST(MimirTests, SearchRelaxedReachabilityDeterminismTest)
{
    for (const auto& domain_name : { "childsnack", "logistics", "rovers", "philosophers" })
    {
        const auto problem_a = parse_fixture(domain_name, "test_problem.pddl");
        const auto problem_b = parse_fixture(domain_name, "test_problem.pddl");

        const auto first = RelaxedReachability::create(problem_a);
        const auto second = RelaxedReachability::create(problem_b);

        /* Byte-for-byte identical tables, in the same tuple order: the engine iterates only vectors, never a
           hash map, so the insertion order is a function of the input. */
        auto render_table = [](const Problem& problem, const ReachabilityTable& table)
        {
            auto stream = std::ostringstream {};
            auto objects = ObjectList {};
            for (const auto predicate : problem->get_domain()->get_predicates<FluentTag>())
            {
                const auto tuples = table.get_reachable_tuples(predicate);
                for (size_t i = 0; i < tuples.size(); ++i)
                {
                    tuples.write(i, objects);
                    stream << render(predicate, objects) << "\n";
                }
            }
            return stream.str();
        };

        EXPECT_EQ(render_table(problem_a, first->get_table()), render_table(problem_b, second->get_table())) << domain_name;
        EXPECT_EQ(first->get_statistics().num_reachable_fluent_atoms, second->get_statistics().num_reachable_fluent_atoms) << domain_name;
        EXPECT_EQ(first->get_statistics().num_join_steps, second->get_statistics().num_join_steps) << domain_name;
        EXPECT_EQ(first->get_statistics().num_fixpoint_rounds, second->get_statistics().num_fixpoint_rounds) << domain_name;

        /* The same for a restricted query. */
        auto forbidden_a = GroundAtomList<FluentTag> {};
        auto forbidden_b = GroundAtomList<FluentTag> {};
        for (const auto predicate : problem_a->get_domain()->get_predicates<FluentTag>())
        {
            const auto tuples = first->get_reachable_tuples(predicate);
            if (tuples.size() > 0)
            {
                forbidden_a.push_back(problem_a->get_or_create_ground_atom<FluentTag>(predicate, tuples[0]));
                auto names = std::vector<std::string> {};
                for (const auto object : tuples[0])
                {
                    names.push_back(object->get_name());
                }
                auto objects = ObjectList {};
                for (const auto& name : names)
                {
                    objects.push_back(problem_b->get_problem_or_domain_object(name));
                }
                forbidden_b.push_back(
                    problem_b->get_or_create_ground_atom<FluentTag>(problem_b->get_domain()->get_predicate<FluentTag>(predicate->get_name()), objects));
                break;
            }
        }
        ASSERT_FALSE(forbidden_a.empty()) << domain_name;

        EXPECT_EQ(render_table(problem_a, first->compute_restricted(forbidden_a)), render_table(problem_b, second->compute_restricted(forbidden_b)))
            << domain_name;
    }
}

/**
 * 4. Performance table.
 *
 * Reads the instances from `MIMIR_RELAXED_REACHABILITY_BENCH_DIR`, which is expected to point at
 * `hierarchical/data/pddl`. Skipped when the variable is unset, so the suite stays self-contained.
 */

TEST(MimirTests, SearchRelaxedReachabilityPerformanceTest)
{
    const auto* root_env = std::getenv("MIMIR_RELAXED_REACHABILITY_BENCH_DIR");
    if (root_env == nullptr)
    {
        GTEST_SKIP() << "set MIMIR_RELAXED_REACHABILITY_BENCH_DIR to hierarchical/data/pddl to run the performance table";
    }
    const auto root = fs::path(root_env);

    struct Row
    {
        std::string label;
        size_t num_objects = 0;
        double parse_ms = 0;
        double compile_ms = 0;
        double planning_fixpoint_ms = 0;
        double fixpoint_ms = 0;
        size_t num_atoms = 0;
        size_t num_rules = 0;
        size_t num_steps = 0;
        size_t num_aux_tuples = 0;
        double restricted_mean_ms = 0;
        double restricted_max_ms = 0;
        double goal_query_mean_ms = 0;
        double rss_delta_mb = 0;
        double peak_rss_mb = 0;
    };

    /* The largest test instance of every domain, plus the childsnack ladder. */
    const auto entries = std::vector<std::pair<std::string, std::string>> {
        { "barman-ipc", "" },       { "blocks", "" },
        { "blocksworld-ipc-enhanced", "" }, { "childsnack-ipc", "" },
        { "ferry-ipc", "" },        { "floortile-ipc", "" },
        { "logistics-6", "" },      { "miconic-ipc", "" },
        { "rovers-ipc", "" },       { "satellite-ipc", "" },
        { "sokoban-ipc", "" },      { "spanner-ipc", "" },
        { "transport-ipc", "" },    { "childsnack-ipc", "p05-hard.pddl" },
        { "childsnack-ipc", "p10-hard.pddl" }, { "childsnack-ipc", "p20-hard.pddl" },
        { "childsnack-ipc", "p30-hard.pddl" },
    };

    /* Optionally restrict to one row, so a shell loop can give every instance its own process and therefore a
       peak RSS that is only about that instance. */
    const auto* only_env = std::getenv("MIMIR_RELAXED_REACHABILITY_BENCH_ONLY");
    const auto only = std::string(only_env ? only_env : "");

    auto rows = std::vector<Row> {};

    for (const auto& [domain_name, explicit_problem] : entries)
    {
        if (!only.empty() && only != domain_name && only != domain_name + ":" + explicit_problem)
        {
            continue;
        }
        auto domain_dir = root / domain_name;
        if (!fs::exists(domain_dir))
        {
            domain_dir = root / (domain_name == "barman-ipc" ? "barman" : domain_name);
        }
        if (!fs::exists(domain_dir))
        {
            std::cout << "[relaxed-reachability] skipping " << domain_name << ": not under " << root << std::endl;
            continue;
        }
        /* These families keep the domain next to the instances. */
        auto domain_file = domain_dir / "domain.pddl";
        if (!fs::exists(domain_file))
        {
            domain_file = domain_dir / "test" / "domain.pddl";
        }
        auto problem_file = fs::path {};
        if (!explicit_problem.empty())
        {
            problem_file = domain_dir / "test" / explicit_problem;
        }
        else
        {
            auto best_size = uintmax_t(0);
            for (const auto& entry : fs::directory_iterator(domain_dir / "test"))
            {
                if (entry.path().extension() != ".pddl" || entry.path().filename() == "domain.pddl")
                {
                    continue;
                }
                const auto size = fs::file_size(entry.path());
                if (size > best_size)
                {
                    best_size = size;
                    problem_file = entry.path();
                }
            }
        }
        if (problem_file.empty() || !fs::exists(problem_file) || !fs::exists(domain_file))
        {
            std::cout << "[relaxed-reachability] skipping " << domain_name << ": no problem file" << std::endl;
            continue;
        }

        auto row = Row {};
        row.label = domain_name + "/" + problem_file.filename().string();

        const auto rss_before = current_resident_bytes();
        const auto parse_start = std::chrono::high_resolution_clock::now();
        const auto problem = ProblemImpl::create(domain_file, problem_file, loki::ParserOptions {});
        row.parse_ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - parse_start).count();
        row.num_objects = problem->get_problem_and_domain_objects().size();

        const auto reachability = RelaxedReachability::create(problem);
        const auto& statistics = reachability->get_statistics();
        row.compile_ms = statistics.compile_time_ms;
        row.planning_fixpoint_ms = statistics.planning_fixpoint_time_ms;
        row.fixpoint_ms = statistics.fixpoint_time_ms;
        row.num_atoms = statistics.num_reachable_fluent_atoms + statistics.num_reachable_derived_atoms;
        row.num_rules = statistics.num_rules;
        row.num_steps = statistics.num_join_steps;
        row.num_aux_tuples = statistics.num_auxiliary_tuples;

        /* 20 restricted queries, each forbidding one random reachable atom. */
        auto candidates = std::vector<std::pair<Predicate<FluentTag>, ObjectList>> {};
        for (const auto predicate : problem->get_domain()->get_predicates<FluentTag>())
        {
            const auto tuples = reachability->get_reachable_tuples(predicate);
            for (size_t i = 0; i < tuples.size(); ++i)
            {
                candidates.emplace_back(predicate, tuples[i]);
            }
        }
        auto rng = std::mt19937(20260905u);
        auto restricted_total = double(0);
        auto goal_total = double(0);
        const auto num_queries = std::min<size_t>(20, candidates.size());
        for (size_t i = 0; i < num_queries; ++i)
        {
            const auto& candidate = candidates[std::uniform_int_distribution<size_t>(0, candidates.size() - 1)(rng)];
            const auto forbidden = RelaxedReachability::ForbiddenAtomList { candidate };

            auto start = std::chrono::high_resolution_clock::now();
            const auto table = reachability->compute_restricted(forbidden);
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();
            restricted_total += elapsed;
            row.restricted_max_ms = std::max(row.restricted_max_ms, elapsed);
            EXPECT_LE(table.get_num_reachable_atoms(), reachability->get_num_reachable_atoms()) << row.label;

            start = std::chrono::high_resolution_clock::now();
            const auto verdict = reachability->is_goal_reachable_without(forbidden);
            goal_total += std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();
            EXPECT_EQ(verdict, table.is_goal_reachable()) << row.label;
        }
        row.restricted_mean_ms = num_queries ? restricted_total / double(num_queries) : 0.0;
        row.goal_query_mean_ms = num_queries ? goal_total / double(num_queries) : 0.0;
        const auto rss_after = current_resident_bytes();
        row.rss_delta_mb = double(rss_after > rss_before ? rss_after - rss_before : 0) / (1024.0 * 1024.0);
        row.peak_rss_mb = double(peak_resident_bytes()) / (1024.0 * 1024.0);

        rows.push_back(row);
        std::cout << "[relaxed-reachability] done " << row.label << std::endl;
    }

    std::cout << "\n=== relaxed reachability performance ===\n";
    std::cout << "instance                                       objs   parse(ms) compile(ms)  plan-fp(ms)  fixpoint(ms)   atoms  rules "
                 "steps  aux-tuples  restr-mean(ms) restr-max(ms) goal-mean(ms)  dRSS(MB)  peakRSS(MB)\n";
    for (const auto& row : rows)
    {
        printf("%-44s %6zu %11.2f %11.2f %12.2f %13.2f %7zu %6zu %6zu %11zu %15.3f %13.3f %13.3f %9.1f %12.1f\n",
               row.label.c_str(),
               row.num_objects,
               row.parse_ms,
               row.compile_ms,
               row.planning_fixpoint_ms,
               row.fixpoint_ms,
               row.num_atoms,
               row.num_rules,
               row.num_steps,
               row.num_aux_tuples,
               row.restricted_mean_ms,
               row.restricted_max_ms,
               row.goal_query_mean_ms,
               row.rss_delta_mb,
               row.peak_rss_mb);
    }
    std::cout << std::endl;

    EXPECT_FALSE(rows.empty());
}

}
