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
#include <stdexcept>
#include <type_traits>
#include <variant>
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
                                                           "relaxed_reachability_axiom",
                                                           "relaxed_reachability_negative_static" };
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
 * 1b. Negative static preconditions: where this engine and `LiftedGrounder` part company.
 */

TEST(MimirTests, SearchRelaxedReachabilityNegativeStaticConditionTest)
{
    const auto problem = parse_fixture("relaxed_reachability_negative_static", "test_problem.pddl");
    const auto object = [&](const std::string& name) { return problem->get_problem_or_domain_object(name); };
    const auto& predicates = problem->get_domain()->get_name_to_predicate<FluentTag>();

    /* Exact: `(not (blocked ?i))` is a static condition, and no action can make a static atom go away, so
       `step1(b)` can never fire and nothing downstream of it is reachable. */
    const auto exact = RelaxedReachability::create(problem);
    EXPECT_TRUE(exact->is_reachable(predicates.at("mid"), { object("a") }));
    EXPECT_TRUE(exact->is_reachable(predicates.at("goal"), { object("a") }));
    EXPECT_FALSE(exact->is_reachable(predicates.at("mid"), { object("b") }));
    EXPECT_FALSE(exact->is_reachable(predicates.at("goal"), { object("b") }));
    EXPECT_TRUE(exact->is_goal_reachable());

    /* `LiftedGrounder`'s delete-free copy drops the negative literal, so its exploration instantiates
       `step1(b)`. `create_ground_actions()` filters that one back out on static applicability -- but
       `step2(b)`, which has no static condition, survives, and its precondition atom `mid(b)` is not
       reachable at all. The grounded universe is therefore a strict superset of the reachable one, which is
       the over-approximation, and it is on the safe side for every consumer. */
    auto grounder = LiftedGrounder(problem);
    auto grounded_step2_b = false;
    for (const auto action : grounder.create_ground_actions())
    {
        if (action->get_action()->get_name() == "step2" && action->get_objects().at(0) == object("b"))
        {
            grounded_step2_b = true;
        }
    }
    EXPECT_TRUE(grounded_step2_b) << "expected the grounder to instantiate an action the engine calls unreachable";

    /* Closing the initial state under the *returned* action list still agrees with the engine atom for atom:
       `mid(b)` is never derived, so `step2(b)` never fires. That is why the exactness test over all 49
       instances comes out clean even though the two disagree about which ground actions exist. */
    const auto reference = GroundReference(problem);
    EXPECT_EQ(engine_fluent_atoms(problem, exact->get_table()), reference_fluent_atoms(problem, reference.close({})));

    /* Turning the filter off reproduces the delete-free exploration's reading, and it reaches strictly more. */
    auto relaxed_options = RelaxedReachabilityOptions {};
    relaxed_options.enforce_negative_static_conditions = false;
    const auto over = RelaxedReachability::create(problem, relaxed_options);
    EXPECT_TRUE(over->is_reachable(predicates.at("mid"), { object("b") }));
    EXPECT_TRUE(over->is_reachable(predicates.at("goal"), { object("b") }));
    EXPECT_GT(over->get_num_reachable_atoms(), exact->get_num_reachable_atoms());
}


/**
 * A. Projected conjunctive queries.
 */

namespace
{

/// @brief The tuples of one predicate as the table sees them: reachable atoms for a fluent or derived
/// predicate, the initial atoms for a static one.
class TupleSource
{
public:
    TupleSource(const Problem& problem, const ReachabilityTable& table) : m_problem(problem)
    {
        for (const auto atom : problem->get_static_initial_atoms())
        {
            m_static[atom->get_predicate()].push_back(atom->get_objects());
        }
        for (const auto predicate : problem->get_domain()->get_predicates<FluentTag>())
        {
            const auto tuples = table.get_reachable_tuples(predicate);
            for (size_t i = 0; i < tuples.size(); ++i)
            {
                m_fluent[predicate].push_back(tuples[i]);
            }
        }
        for (const auto predicate : problem->get_problem_and_domain_derived_predicates())
        {
            const auto tuples = table.get_reachable_tuples(predicate);
            for (size_t i = 0; i < tuples.size(); ++i)
            {
                m_derived[predicate].push_back(tuples[i]);
            }
        }
    }

    const std::vector<ObjectList>& get(const PredicateVariant& predicate) const
    {
        static const auto empty = std::vector<ObjectList> {};
        return std::visit(
            [&](auto concrete) -> const std::vector<ObjectList>&
            {
                using P = typename std::remove_pointer_t<decltype(concrete)>::Type;
                if constexpr (std::is_same_v<P, StaticTag>)
                {
                    const auto it = m_static.find(concrete);
                    return (it == m_static.end()) ? empty : it->second;
                }
                else if constexpr (std::is_same_v<P, FluentTag>)
                {
                    const auto it = m_fluent.find(concrete);
                    return (it == m_fluent.end()) ? empty : it->second;
                }
                else
                {
                    const auto it = m_derived.find(concrete);
                    return (it == m_derived.end()) ? empty : it->second;
                }
            },
            predicate);
    }

private:
    Problem m_problem;
    std::unordered_map<Predicate<StaticTag>, std::vector<ObjectList>> m_static;
    std::unordered_map<Predicate<FluentTag>, std::vector<ObjectList>> m_fluent;
    std::unordered_map<Predicate<DerivedTag>, std::vector<ObjectList>> m_derived;
};

/// @brief The reference: a plain backtracking join over the whole conjunction, collecting the objects each
/// variable takes in a satisfying assignment. This is the thing `project` must never do at scale.
class BruteForceProjection
{
public:
    BruteForceProjection(const Problem& problem, const TupleSource& source, const ConjunctiveQuery& query, size_t node_budget) :
        m_problem(problem),
        m_source(source),
        m_query(query),
        m_budget(node_budget),
        m_binding(query.num_variables, nullptr),
        m_answers(query.num_variables)
    {
        m_positive.reserve(query.literals.size());
        for (const auto& literal : query.literals)
        {
            (literal.polarity ? m_positive : m_negative).push_back(&literal);
        }
        m_exceeded = !search(0);
    }

    bool exceeded_budget() const { return m_exceeded; }

    std::vector<ObjectList> get() const
    {
        auto result = std::vector<ObjectList>(m_query.num_variables);
        for (size_t variable = 0; variable < m_query.num_variables; ++variable)
        {
            auto objects = ObjectList(m_answers[variable].begin(), m_answers[variable].end());
            std::sort(objects.begin(), objects.end(), [](Object lhs, Object rhs) { return lhs->get_index() < rhs->get_index(); });
            result[variable] = std::move(objects);
        }
        return result;
    }

private:
    bool matches(const QueryTerm& term, Object object)
    {
        if (!term.is_variable())
        {
            return term.get_object() == object;
        }
        auto& slot = m_binding[term.get_variable()];
        if (slot == nullptr)
        {
            slot = object;
            m_trail.push_back(term.get_variable());
            return true;
        }
        return slot == object;
    }

    Object resolve(const QueryTerm& term) const { return term.is_variable() ? m_binding[term.get_variable()] : term.get_object(); }

    bool constraints_hold()
    {
        for (const auto& [lhs, rhs] : m_query.equalities)
        {
            if (resolve(lhs) != resolve(rhs))
            {
                return false;
            }
        }
        for (const auto& [lhs, rhs] : m_query.disequalities)
        {
            if (resolve(lhs) == resolve(rhs))
            {
                return false;
            }
        }
        for (const auto* literal : m_negative)
        {
            // A negative fluent or derived literal is what the delete relaxation drops, on both sides.
            if (!std::holds_alternative<Predicate<StaticTag>>(literal->predicate))
            {
                continue;
            }
            auto objects = ObjectList {};
            for (const auto& term : literal->terms)
            {
                objects.push_back(resolve(term));
            }
            for (const auto& tuple : m_source.get(literal->predicate))
            {
                ++m_nodes;
                if (tuple == objects)
                {
                    return false;
                }
            }
        }
        return true;
    }

    /// @brief Returns false when the node budget ran out.
    bool search(size_t index)
    {
        if (m_nodes > m_budget)
        {
            return false;
        }
        if (index == m_positive.size())
        {
            if (constraints_hold())
            {
                for (size_t variable = 0; variable < m_query.num_variables; ++variable)
                {
                    if (m_binding[variable] != nullptr)
                    {
                        m_answers[variable].insert(m_binding[variable]);
                    }
                    else
                    {
                        // A variable no literal binds ranges over every object.
                        for (const auto object : m_problem->get_problem_and_domain_objects())
                        {
                            m_answers[variable].insert(object);
                        }
                    }
                }
            }
            return true;
        }

        const auto* literal = m_positive[index];
        for (const auto& tuple : m_source.get(literal->predicate))
        {
            if (m_nodes++ > m_budget)
            {
                return false;
            }
            if (tuple.size() != literal->terms.size())
            {
                continue;
            }
            const auto mark = m_trail.size();
            auto consistent = true;
            for (size_t position = 0; position < tuple.size(); ++position)
            {
                if (!matches(literal->terms[position], tuple[position]))
                {
                    consistent = false;
                    break;
                }
            }
            if (consistent && !search(index + 1))
            {
                return false;
            }
            while (m_trail.size() > mark)
            {
                m_binding[m_trail.back()] = nullptr;
                m_trail.pop_back();
            }
        }
        return true;
    }

    Problem m_problem;
    const TupleSource& m_source;
    const ConjunctiveQuery& m_query;
    size_t m_budget;
    size_t m_nodes = 0;
    bool m_exceeded = false;
    std::vector<const QueryLiteral*> m_positive;
    std::vector<const QueryLiteral*> m_negative;
    ObjectList m_binding;
    std::vector<uint32_t> m_trail;
    std::vector<std::set<Object>> m_answers;
};

/// @brief The query the lifted generator's joint disambiguation asks: an action schema's precondition
/// conjunction, with the schema's parameters as the query variables.
ConjunctiveQuery query_of_schema(Action action, ConditionalEffect effect, const ObjectList& partial_binding)
{
    auto query = ConjunctiveQuery {};
    auto num_slots = size_t(0);
    const auto note = [&](const ParameterList& parameters)
    {
        for (const auto parameter : parameters)
        {
            num_slots = std::max(num_slots, size_t(parameter->get_variable()->get_parameter_index()) + 1);
        }
    };
    note(action->get_conjunctive_condition()->get_parameters());
    note(effect->get_conjunctive_condition()->get_parameters());
    query.num_variables = num_slots;

    const auto to_term = [&](Term term)
    {
        const auto& variant = term->get_variant();
        if (std::holds_alternative<Object>(variant))
        {
            return QueryTerm::of_object(std::get<Object>(variant));
        }
        const auto slot = size_t(std::get<Variable>(variant)->get_parameter_index());
        if (slot < partial_binding.size() && partial_binding[slot] != nullptr)
        {
            return QueryTerm::of_object(partial_binding[slot]);
        }
        return QueryTerm::of_variable(uint32_t(slot));
    };

    for (const auto condition : { action->get_conjunctive_condition(), effect->get_conjunctive_condition() })
    {
        const auto collect = [&](auto&& literals)
        {
            for (const auto literal : literals)
            {
                auto entry = QueryLiteral {};
                entry.predicate = literal->get_atom()->get_predicate();
                entry.polarity = literal->get_polarity();
                for (const auto term : literal->get_atom()->get_terms())
                {
                    entry.terms.push_back(to_term(term));
                }
                // mimir keeps PDDL equality as a static predicate whose atoms it does not always materialise,
                // so it is stated as a constraint rather than as a literal -- the same reading the engine has.
                if (entry.predicate.index() == 0 && std::get<Predicate<StaticTag>>(entry.predicate)->get_name() == "=" && entry.terms.size() == 2)
                {
                    (entry.polarity ? query.equalities : query.disequalities).emplace_back(entry.terms[0], entry.terms[1]);
                    continue;
                }
                query.literals.push_back(std::move(entry));
            }
        };
        collect(condition->get_literals<StaticTag>());
        collect(condition->get_literals<FluentTag>());
        collect(condition->get_literals<DerivedTag>());
    }
    return query;
}

}

TEST(MimirTests, SearchRelaxedReachabilityProjectTest)
{
    static const auto instances = std::vector<std::pair<std::string, std::string>> {
        { "blocks_4", "test_problem.pddl" },   { "gripper", "test_problem.pddl" },     { "delivery", "test_problem.pddl" },
        { "childsnack", "test_problem.pddl" }, { "miconic", "test_problem.pddl" },     { "spanner", "test_problem.pddl" },
        { "logistics", "test_problem.pddl" },  { "rovers", "test_problem.pddl" },      { "satellite", "test_problem.pddl" },
        { "ferry", "test_problem.pddl" },      { "transport", "test_problem.pddl" },   { "barman", "test_problem.pddl" },
        { "philosophers", "test_problem.pddl" },
        { "relaxed_reachability_axiom", "test_problem.pddl" },
        { "relaxed_reachability_chain", "test_problem.pddl" },
        { "relaxed_reachability_negative_static", "test_problem.pddl" },
    };

    auto num_queries = size_t(0);
    auto num_skipped = size_t(0);
    auto rng = std::mt19937(20260906u);

    /* A handful of IPC instances on top of the fixtures, when the benchmark data is available. */
    auto parsed = std::vector<std::pair<std::string, Problem>> {};
    for (const auto& [domain_name, problem_filename] : instances)
    {
        parsed.emplace_back(domain_name + "/" + problem_filename, parse_fixture(domain_name, problem_filename));
    }
    if (const auto* root_env = std::getenv("MIMIR_RELAXED_REACHABILITY_BENCH_DIR"))
    {
        const auto root = fs::path(root_env);
        for (const auto& domain_name : { "childsnack-ipc", "ferry-ipc", "blocksworld-ipc-enhanced", "sokoban-ipc", "rovers-ipc" })
        {
            const auto directory = root / domain_name / "test";
            const auto domain_file = fs::exists(directory / "domain.pddl") ? directory / "domain.pddl" : root / domain_name / "domain.pddl";
            const auto problem_file = directory / "p01-easy.pddl";
            if (fs::exists(domain_file) && fs::exists(problem_file))
            {
                parsed.emplace_back(std::string(domain_name) + "/p01-easy.pddl", ProblemImpl::create(domain_file, problem_file, loki::ParserOptions {}));
            }
        }
    }

    for (const auto& [label, problem] : parsed)
    {
        const auto reachability = RelaxedReachability::create(problem);
        const auto& table = reachability->get_table();
        const auto source = TupleSource(problem, table);
        const auto& objects = problem->get_problem_and_domain_objects();

        for (const auto action : problem->get_domain()->get_actions())
        {
            for (const auto effect : action->get_conditional_effects())
            {
                /* Unbound, then with one parameter pinned -- the two shapes the generator asks, and the second
                   exercises the constant table on a plan compiled for the first. */
                for (auto trial = 0; trial < 3; ++trial)
                {
                    auto binding = ObjectList {};
                    if (trial > 0 && !objects.empty())
                    {
                        binding.assign(action->get_arity(), nullptr);
                        if (!binding.empty())
                        {
                            binding[std::uniform_int_distribution<size_t>(0, binding.size() - 1)(rng)] =
                                objects[std::uniform_int_distribution<size_t>(0, objects.size() - 1)(rng)];
                        }
                    }
                    const auto query = query_of_schema(action, effect, binding);
                    if (query.num_variables == 0)
                    {
                        continue;
                    }

                    auto reference = BruteForceProjection(problem, source, query, 300000);
                    if (reference.exceeded_budget())
                    {
                        ++num_skipped;
                        continue;
                    }
                    ++num_queries;
                    EXPECT_EQ(table.project(query), reference.get()) << label << " / " << action->get_name() << " trial " << trial;
                }
            }
        }
    }

    std::cout << "[relaxed-reachability] project: " << num_queries << " queries checked against the brute-force join, " << num_skipped
              << " skipped on budget" << std::endl;
    EXPECT_GT(num_queries, 60u);
}


/**
 * A (adversarial). The cached-plan path: same shape, different constants, different tables, interleaved.
 *
 * A plan is cached by query shape and instantiated with a per-call constant table, and a query database
 * resolves its base relations through whichever table it was opened on. Both of those are places where state
 * from an earlier call could leak into a later one, and neither shows up on a first call. These tests drive
 * the sequences the generator actually produces.
 */

namespace
{

/// @brief The `unstack` precondition conjunction with `?underob` pinned -- exactly what the generator asks
/// when it disambiguates that achiever, static type literals included.
ConjunctiveQuery unstack_query(const Problem& problem, Object pinned)
{
    for (const auto action : problem->get_domain()->get_actions())
    {
        if (action->get_name() != "unstack")
        {
            continue;
        }
        auto binding = ObjectList(action->get_arity(), nullptr);
        if (pinned != nullptr)
        {
            binding.at(1) = pinned;
        }
        return query_of_schema(action, action->get_conditional_effects().front(), binding);
    }
    return ConjunctiveQuery {};
}

/// @brief The same conjunction, but pinning `?underob` through an equality instead of by substituting the
/// object into the terms. Same predicates, different variable pattern, and a different path through the
/// planner: the variable keeps a slot of the constant table rather than disappearing from the rule.
ConjunctiveQuery unstack_query_pinned_by_equality(const Problem& problem, Object pinned)
{
    auto query = unstack_query(problem, nullptr);
    query.equalities.emplace_back(QueryTerm::of_variable(1), QueryTerm::of_object(pinned));
    return query;
}

/// @brief `unstack` with the FIRST parameter pinned: the same predicates as `unstack_query`, a different
/// variable pattern, so the two must not share a cached plan.
ConjunctiveQuery unstack_query_pin_first(const Problem& problem, Object pinned)
{
    for (const auto action : problem->get_domain()->get_actions())
    {
        if (action->get_name() != "unstack")
        {
            continue;
        }
        auto binding = ObjectList(action->get_arity(), nullptr);
        binding.at(0) = pinned;
        return query_of_schema(action, action->get_conditional_effects().front(), binding);
    }
    return ConjunctiveQuery {};
}

/// @brief The `stack` conjunction with `?underob` pinned: a different shape, to interleave with.
ConjunctiveQuery stack_query(const Problem& problem, Object pinned)
{
    for (const auto action : problem->get_domain()->get_actions())
    {
        if (action->get_name() != "stack")
        {
            continue;
        }
        auto binding = ObjectList(action->get_arity(), nullptr);
        binding.at(1) = pinned;
        return query_of_schema(action, action->get_conditional_effects().front(), binding);
    }
    return ConjunctiveQuery {};
}

std::vector<std::string> names(const ObjectList& objects)
{
    auto result = std::vector<std::string> {};
    for (const auto object : objects)
    {
        result.push_back(object->get_name());
    }
    std::sort(result.begin(), result.end());
    return result;
}

}

TEST(MimirTests, SearchRelaxedReachabilityProjectCachedPlanSequenceTest)
{
    const auto problem = parse_fixture("blocks_4", "test_problem.pddl");
    const auto object = [&](const std::string& name) { return problem->get_problem_or_domain_object(name); };
    const auto reachability = RelaxedReachability::create(problem);
    const auto& unrestricted = reachability->get_table();
    const auto& fluent = problem->get_domain()->get_name_to_predicate<FluentTag>();

    auto num_calls = size_t(0);
    const auto check = [&](const ReachabilityTable& table, const TupleSource& source, const ConjunctiveQuery& query, const std::string& label)
        -> std::vector<ObjectList>
    {
        auto reference = BruteForceProjection(problem, source, query, 300000);
        EXPECT_FALSE(reference.exceeded_budget()) << label;
        const auto expected = reference.get();
        const auto actual = table.project(query);
        EXPECT_EQ(actual.size(), expected.size()) << label;
        for (size_t variable = 0; variable < std::min(actual.size(), expected.size()); ++variable)
        {
            EXPECT_EQ(names(actual[variable]), names(expected[variable])) << label << " variable " << variable;
        }
        ++num_calls;
        return actual;
    };

    /* 1. Same shape, different constants, in a row on one table. Includes the same object pinned twice in a
          row, and two objects of different index pinned in both orders. */
    {
        const auto source = TupleSource(problem, unrestricted);
        for (const auto& name : { "b1", "b1", "b3", "b1", "b3", "b2", "b3", "b2", "b1" })
        {
            check(unrestricted, source, unstack_query(problem, object(name)), std::string("R / unstack ?underob=") + name);
        }
    }

    /* 2. The same shape across three tables, alternating, with the plan cached from the first. Nothing of the
          first table -- relation ids, `$all_objects`, the stability of a right-hand side, a tuple count -- may
          survive into the next. */
    {
        const auto clear_b3 = problem->get_or_create_ground_atom<FluentTag>(fluent.at("clear"), { object("b3") });
        const auto on_b1_b3 = problem->get_or_create_ground_atom<FluentTag>(fluent.at("on"), { object("b1"), object("b3") });
        const auto without_clear_b3 = reachability->compute_restricted(GroundAtomList<FluentTag> { clear_b3 });
        const auto without_on_b1_b3 = reachability->compute_restricted(GroundAtomList<FluentTag> { on_b1_b3 });

        const auto sources = std::vector<TupleSource> { TupleSource(problem, unrestricted),
                                                        TupleSource(problem, without_clear_b3),
                                                        TupleSource(problem, without_on_b1_b3) };
        const auto tables = std::vector<const ReachabilityTable*> { &unrestricted, &without_clear_b3, &without_on_b1_b3 };
        const auto labels = std::vector<std::string> { "R", "R-clear(b3)", "R-on(b1,b3)" };

        for (const auto& name : { "b3", "b1", "b2", "b3" })
        {
            for (size_t index = 0; index < tables.size(); ++index)
            {
                check(*tables[index], sources[index], unstack_query(problem, object(name)), labels[index] + " / unstack ?underob=" + name);
            }
        }
    }

    /* 3. Interleaved shapes A, B, A', B' on the restricted table, with the pinned atom itself forbidden. */
    {
        const auto clear_b3 = problem->get_or_create_ground_atom<FluentTag>(fluent.at("clear"), { object("b3") });
        const auto restricted = reachability->compute_restricted(GroundAtomList<FluentTag> { clear_b3 });
        const auto source = TupleSource(problem, restricted);

        check(restricted, source, unstack_query(problem, object("b1")), "A  unstack ?underob=b1");
        check(restricted, source, stack_query(problem, object("b1")), "B  stack ?underob=b1");
        const auto a_prime = check(restricted, source, unstack_query(problem, object("b3")), "A' unstack ?underob=b3");
        check(restricted, source, stack_query(problem, object("b2")), "B' stack ?underob=b2");

        /* The generator's case, spelled out: with `clear(b3)` forbidden, nothing can be stacked onto b3, so
           `on(?ob, b3)` is only the initial `on(b1, b3)` and the answer for `?ob` is exactly {b1}. */
        ASSERT_FALSE(a_prime.empty());
        EXPECT_EQ(names(a_prime[0]), (std::vector<std::string> { "b1" })) << "A' after A, B on the same cached shape";

        /* And asking it once more, after two other shapes have run, must not change it. */
        check(restricted, source, stack_query(problem, object("b3")), "B'' stack ?underob=b3");
        const auto again = check(restricted, source, unstack_query(problem, object("b3")), "A'' unstack ?underob=b3");
        EXPECT_EQ(names(again[0]), (std::vector<std::string> { "b1" })) << "A'' after further interleaving";

        /* 3b. The same conjunction pinned through an equality rather than by substitution: same predicates,
               a different variable pattern, and the path where a variable keeps a constant-table slot. */
        for (const auto& name : { "b1", "b3", "b3", "b2", "b3" })
        {
            const auto answers =
                check(restricted, source, unstack_query_pinned_by_equality(problem, object(name)), std::string("EQ unstack ?underob=") + name);
            if (std::string(name) == "b3")
            {
                EXPECT_EQ(names(answers[0]), (std::vector<std::string> { "b1" })) << "equality-pinned A' for ?ob";
                EXPECT_EQ(names(answers[1]), (std::vector<std::string> { "b3" })) << "equality-pinned A' for ?underob";
            }
        }

        /* 3c. Same predicates, other position pinned. Two shapes that differ only in which term is a constant
               must not collide in the plan cache. */
        for (const auto& name : { "b1", "b3", "b2", "b1" })
        {
            check(restricted, source, unstack_query_pin_first(problem, object(name)), std::string("PIN-FIRST unstack ?ob=") + name);
            check(restricted, source, unstack_query(problem, object(name)), std::string("PIN-SECOND unstack ?underob=") + name);
        }
    }

    /* 4. A malformed query is refused, not absorbed. Until this check existed, a variable index at or past
          `num_variables` was resolved to *constant slot 0* -- the first constant the query happened to carry.
          A caller that numbers its variables by parameter slot but sizes `num_variables` by how many are free
          (natural when the rest are pinned) hits exactly that whenever the free parameter is not the first
          one, and gets a plausible wrong answer that changes with the constants. It is now a named error. */
    {
        auto malformed = unstack_query(problem, nullptr);  ///< uses variables 0 and 1
        malformed.num_variables = 1;
        EXPECT_THROW((void) unrestricted.project(malformed), std::invalid_argument);

        auto malformed_pin_first = unstack_query_pin_first(problem, object("b3"));  ///< the free variable is 1
        malformed_pin_first.num_variables = 1;
        EXPECT_THROW((void) unrestricted.project(malformed_pin_first), std::invalid_argument);

        auto malformed_equality = unstack_query(problem, nullptr);
        malformed_equality.equalities.emplace_back(QueryTerm::of_variable(7), QueryTerm::of_object(object("b3")));
        EXPECT_THROW((void) unrestricted.project(malformed_equality), std::invalid_argument);
    }

    std::cout << "[relaxed-reachability] cached-plan sequence: " << num_calls << " project calls, each against the brute-force join"
              << std::endl;
    EXPECT_GT(num_calls, 20u);
}

/**
 * B. Witness derivations.
 */

TEST(MimirTests, SearchRelaxedReachabilityWitnessTest)
{
    static const auto instances = std::vector<std::pair<std::string, std::string>> {
        { "blocks_4", "test_problem.pddl" },   { "gripper", "test_problem.pddl" },   { "delivery", "test_problem.pddl" },
        { "childsnack", "test_problem.pddl" }, { "miconic", "test_problem.pddl" },   { "spanner", "test_problem.pddl" },
        { "logistics", "test_problem.pddl" },  { "rovers", "test_problem.pddl" },    { "satellite", "test_problem.pddl" },
        { "ferry", "test_problem.pddl" },      { "transport", "test_problem.pddl" }, { "philosophers", "test_problem.pddl" },
        { "relaxed_reachability_axiom", "test_problem.pddl" },
        { "relaxed_reachability_chain", "test_problem.pddl" },
    };

    auto rng = std::mt19937(20260906u);
    auto num_checked = size_t(0);
    auto num_reachable_without = size_t(0);
    auto num_pairs = size_t(0);

    for (const auto& [domain_name, problem_filename] : instances)
    {
        const auto problem = parse_fixture(domain_name, problem_filename);
        const auto label = domain_name + "/" + problem_filename;
        const auto reachability = RelaxedReachability::create(problem);
        const auto& table = reachability->get_table();
        ASSERT_TRUE(table.has_witnesses()) << label;

        auto atoms = std::vector<std::pair<Predicate<FluentTag>, ObjectList>> {};
        for (const auto predicate : problem->get_domain()->get_predicates<FluentTag>())
        {
            const auto tuples = table.get_reachable_tuples(predicate);
            for (size_t i = 0; i < tuples.size(); ++i)
            {
                atoms.emplace_back(predicate, tuples[i]);
            }
        }
        ASSERT_FALSE(atoms.empty()) << label;

        /* Every atom against every single-atom forbidden set the fixture affords, plus random sets. */
        auto forbidden_sets = std::vector<RelaxedReachability::ForbiddenAtomList> {};
        for (const auto& atom : atoms)
        {
            forbidden_sets.push_back({ atom });
        }
        for (auto trial = 0; trial < 20; ++trial)
        {
            auto set = RelaxedReachability::ForbiddenAtomList {};
            const auto size = std::uniform_int_distribution<size_t>(1, std::min<size_t>(4, atoms.size()))(rng);
            for (size_t i = 0; i < size; ++i)
            {
                set.push_back(atoms[std::uniform_int_distribution<size_t>(0, atoms.size() - 1)(rng)]);
            }
            forbidden_sets.push_back(std::move(set));
        }

        for (const auto& forbidden : forbidden_sets)
        {
            const auto restricted = reachability->compute_restricted(forbidden);
            const auto witnesses = table.witness_query(forbidden);
            for (const auto& [predicate, tuple] : atoms)
            {
                const auto verdict = witnesses.avoids(predicate, tuple);
                ++num_checked;
                if (verdict == WitnessVerdict::REACHABLE_WITHOUT)
                {
                    ++num_reachable_without;
                    // The soundness the caller relies on: a yes must really be reachable without the set.
                    EXPECT_TRUE(restricted.is_reachable(predicate, tuple)) << label << ": witness claimed an unreachable atom";
                }
            }
            ++num_pairs;
        }
    }

    std::cout << "[relaxed-reachability] witnesses: " << num_checked << " (atom, forbidden set) verdicts over " << num_pairs
              << " forbidden sets; " << num_reachable_without << " REACHABLE_WITHOUT ("
              << (100.0 * double(num_reachable_without) / double(std::max<size_t>(1, num_checked))) << "%), all confirmed by compute_restricted"
              << std::endl;
    EXPECT_GT(num_checked, 5000u);

    /* Without recording, the query is refused rather than silently answering UNKNOWN to everything. */
    const auto problem = parse_fixture("gripper", "test_problem.pddl");
    auto options = RelaxedReachabilityOptions {};
    options.record_witnesses = false;
    const auto without = RelaxedReachability::create(problem, options);
    EXPECT_FALSE(without->get_table().has_witnesses());
    EXPECT_THROW((void) without->get_table().witness_query(RelaxedReachability::ForbiddenAtomList {}), std::logic_error);
}


/**
 * B (at scale). Witness soundness on IPC instances, and the coverage the generator would see.
 *
 * Env-gated on `MIMIR_RELAXED_REACHABILITY_BENCH_DIR` because the instances live outside the repo.
 */

TEST(MimirTests, SearchRelaxedReachabilityWitnessScaleTest)
{
    const auto* root_env = std::getenv("MIMIR_RELAXED_REACHABILITY_BENCH_DIR");
    if (root_env == nullptr)
    {
        GTEST_SKIP() << "set MIMIR_RELAXED_REACHABILITY_BENCH_DIR to hierarchical/data/pddl to run the witness scale test";
    }
    const auto root = fs::path(root_env);

    auto rng = std::mt19937(20260906u);

    for (const auto& domain_name : { "ferry-ipc", "blocksworld-ipc-enhanced" })
    {
        const auto directory = root / domain_name / "test";
        const auto domain_file = fs::exists(directory / "domain.pddl") ? directory / "domain.pddl" : root / domain_name / "domain.pddl";
        const auto problem_file = directory / "p30-hard.pddl";
        if (!fs::exists(domain_file) || !fs::exists(problem_file))
        {
            std::cout << "[relaxed-reachability] skipping witness scale on " << domain_name << std::endl;
            continue;
        }

        const auto problem = ProblemImpl::create(domain_file, problem_file, loki::ParserOptions {});
        const auto reachability = RelaxedReachability::create(problem);
        const auto& table = reachability->get_table();
        ASSERT_TRUE(table.has_witnesses()) << domain_name;

        /* Every reachable atom, grouped by predicate. */
        auto atoms_by_predicate = std::vector<std::pair<Predicate<FluentTag>, std::vector<ObjectList>>> {};
        auto all_atoms = std::vector<std::pair<Predicate<FluentTag>, ObjectList>> {};
        for (const auto predicate : problem->get_domain()->get_predicates<FluentTag>())
        {
            auto tuples = std::vector<ObjectList> {};
            const auto view = table.get_reachable_tuples(predicate);
            for (size_t i = 0; i < view.size(); ++i)
            {
                tuples.push_back(view[i]);
                all_atoms.emplace_back(predicate, tuples.back());
            }
            if (!tuples.empty())
            {
                atoms_by_predicate.emplace_back(predicate, std::move(tuples));
            }
        }
        ASSERT_FALSE(all_atoms.empty()) << domain_name;

        /* (a) 200 random (atom, forbidden set) pairs: a yes must survive `compute_restricted`. */
        auto num_pairs = size_t(0);
        auto num_sound_yes = size_t(0);
        for (auto trial = 0; trial < 200; ++trial)
        {
            auto forbidden = RelaxedReachability::ForbiddenAtomList {};
            const auto size = std::uniform_int_distribution<size_t>(1, 3)(rng);
            for (size_t i = 0; i < size; ++i)
            {
                forbidden.push_back(all_atoms[std::uniform_int_distribution<size_t>(0, all_atoms.size() - 1)(rng)]);
            }
            const auto& [predicate, objects] = all_atoms[std::uniform_int_distribution<size_t>(0, all_atoms.size() - 1)(rng)];

            const auto witnesses = table.witness_query(forbidden);
            ++num_pairs;
            if (witnesses.avoids(predicate, objects) == WitnessVerdict::REACHABLE_WITHOUT)
            {
                ++num_sound_yes;
                const auto restricted = reachability->compute_restricted(forbidden);
                EXPECT_TRUE(restricted.is_reachable(predicate, objects)) << domain_name << ": witness claimed an unreachable atom";
            }
        }

        /* (b) The coverage the generator would see. The ask set is approximated as: for every fact landmark
               atom the lifted generator produces, forbid that one atom and ask every reachable atom of the
               same predicate -- the "same predicate family" variant the brief allows. Both are capped. */
        const auto landmarks = landmarks::LiftedFactLandmarkGenerator::create(problem, {});
        auto landmark_atoms = std::vector<GroundAtom<FluentTag>> {};
        for (const auto atom_index : landmarks->get_landmark_atom_indices())
        {
            landmark_atoms.push_back(problem->get_repositories().get_ground_atom<FluentTag>(atom_index));
        }
        const auto max_landmarks = std::min<size_t>(100, landmark_atoms.size());
        const auto max_atoms_per_landmark = size_t(2000);

        auto num_asks = size_t(0);
        auto num_yes = size_t(0);
        const auto start = std::chrono::high_resolution_clock::now();
        for (size_t index = 0; index < max_landmarks; ++index)
        {
            const auto landmark = landmark_atoms[index];
            const auto forbidden = RelaxedReachability::ForbiddenAtomList { { landmark->get_predicate(), landmark->get_objects() } };
            const auto witnesses = table.witness_query(forbidden);

            for (const auto& [predicate, tuples] : atoms_by_predicate)
            {
                if (predicate != landmark->get_predicate())
                {
                    continue;
                }
                const auto limit = std::min(max_atoms_per_landmark, tuples.size());
                for (size_t i = 0; i < limit; ++i)
                {
                    ++num_asks;
                    if (witnesses.avoids(predicate, tuples[i]) == WitnessVerdict::REACHABLE_WITHOUT)
                    {
                        ++num_yes;
                    }
                }
            }
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();

        std::cout << "[relaxed-reachability] witness scale " << domain_name << "/p30-hard: " << num_pairs << " random pairs ("
                  << num_sound_yes << " REACHABLE_WITHOUT, all confirmed); " << landmark_atoms.size() << " fact landmarks, "
                  << max_landmarks << " sampled; " << num_asks << " asks in " << elapsed << " ms, " << num_yes << " REACHABLE_WITHOUT ("
                  << (100.0 * double(num_yes) / double(std::max<size_t>(1, num_asks))) << "%)" << std::endl;
        EXPECT_GT(num_asks, 0u);
    }
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
        double fixpoint_ms = 0;
        size_t num_atoms = 0;
        size_t num_rules = 0;
        size_t num_steps = 0;
        size_t num_aux_tuples = 0;
        size_t num_witnesses = 0;
        double restricted_mean_ms = 0;
        double restricted_max_ms = 0;
        double goal_query_mean_ms = 0;
        double parse_rss_mb = 0;   ///< what mimir's parser cost, so a large row can be attributed
        double engine_rss_mb = 0;  ///< what the reachability engine added on top of the parsed problem
        double peak_rss_mb = 0;
    };

    /* The largest test instance of every domain, plus the childsnack ladder. */
    const auto entries = std::vector<std::pair<std::string, std::string>> {
        { "barman-ipc", "" },
        { "blocks", "" },
        { "blocksworld-ipc-enhanced", "" },
        { "childsnack-ipc", "" },
        { "ferry-ipc", "" },
        { "floortile-ipc", "" },
        { "logistics-6", "" },
        { "miconic-ipc", "" },
        { "rovers-ipc", "" },
        { "satellite-ipc", "" },
        { "sokoban-ipc", "" },
        { "spanner-ipc", "" },
        { "transport-ipc", "" },
        /* The childsnack ladder: the instance family the engine exists for. */
        { "childsnack-ipc", "p05-hard.pddl" },
        { "childsnack-ipc", "p10-hard.pddl" },
        { "childsnack-ipc", "p13-hard.pddl" },
        { "childsnack-ipc", "p15-hard.pddl" },
        { "childsnack-ipc", "p20-hard.pddl" },
        { "childsnack-ipc", "p25-hard.pddl" },
        { "childsnack-ipc", "p30-hard.pddl" },
    };

    /* Optionally restrict to one row, so a shell loop can give every instance its own process and therefore a
       peak RSS that is only about that instance. */
    const auto* only_env = std::getenv("MIMIR_RELAXED_REACHABILITY_BENCH_ONLY");
    const auto only = std::string(only_env ? only_env : "");

    /* `MIMIR_RELAXED_REACHABILITY_BENCH_NO_WITNESS=1` runs the table with derivation recording off, which is
       how the cost of `record_witnesses` was measured. */
    auto options = RelaxedReachabilityOptions {};
    options.record_witnesses = (std::getenv("MIMIR_RELAXED_REACHABILITY_BENCH_NO_WITNESS") == nullptr);

    auto rows = std::vector<Row> {};

    for (const auto& [domain_name, explicit_problem] : entries)
    {
        // "<domain>" selects the largest-instance row, "<domain>:<file>" one explicit row, so that a shell loop
        // can give every row its own process and therefore a peak RSS that is only about that instance.
        if (!only.empty())
        {
            const auto matches = (only.find(':') == std::string::npos) ? (only == domain_name && explicit_problem.empty()) :
                                                                        (only == domain_name + ":" + explicit_problem);
            if (!matches)
            {
                continue;
            }
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
        const auto rss_after_parse = current_resident_bytes();
        row.parse_rss_mb = double(rss_after_parse > rss_before ? rss_after_parse - rss_before : 0) / (1024.0 * 1024.0);

        const auto reachability = RelaxedReachability::create(problem, options);
        const auto& statistics = reachability->get_statistics();
        row.compile_ms = statistics.compile_time_ms;
        row.fixpoint_ms = statistics.fixpoint_time_ms;
        row.num_atoms = statistics.num_reachable_fluent_atoms + statistics.num_reachable_derived_atoms;
        row.num_rules = statistics.num_rules;
        row.num_steps = statistics.num_join_steps;
        row.num_aux_tuples = statistics.num_auxiliary_tuples;
        row.num_witnesses = reachability->get_table().get_num_witnesses();

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
        row.engine_rss_mb = double(rss_after > rss_after_parse ? rss_after - rss_after_parse : 0) / (1024.0 * 1024.0);
        row.peak_rss_mb = double(peak_resident_bytes()) / (1024.0 * 1024.0);

        rows.push_back(row);
        std::cout << "[relaxed-reachability] done " << row.label << std::endl;
    }

    std::cout << "\n=== relaxed reachability performance ===\n";
    std::cout << "instance                                       objs   parse(ms) compile(ms) fixpoint(ms)   atoms  rules "
                 "steps  aux-tuples   witnesses  restr-mean(ms) restr-max(ms) goal-mean(ms) parseRSS(MB) engineRSS(MB) peakRSS(MB)\n";
    for (const auto& row : rows)
    {
        printf("%-44s %6zu %11.2f %11.2f %12.2f %7zu %6zu %6zu %11zu %11zu %15.3f %13.3f %13.3f %12.1f %13.1f %11.1f\n",
               row.label.c_str(),
               row.num_objects,
               row.parse_ms,
               row.compile_ms,
               row.fixpoint_ms,
               row.num_atoms,
               row.num_rules,
               row.num_steps,
               row.num_aux_tuples,
               row.num_witnesses,
               row.restricted_mean_ms,
               row.restricted_max_ms,
               row.goal_query_mean_ms,
               row.parse_rss_mb,
               row.engine_rss_mb,
               row.peak_rss_mb);
    }
    std::cout << std::endl;

    EXPECT_FALSE(rows.empty());
}

}
