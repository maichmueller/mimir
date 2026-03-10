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

#include "mimir/search/algorithms/iw.hpp"

#include "mimir/formalism/declarations.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/algorithms.hpp"
#include "mimir/search/algorithms/iw/event_handlers.hpp"
#include "mimir/search/algorithms/iw/pruning_strategy.hpp"
#include "mimir/search/algorithms/iw/tuple_index_generators.hpp"
#include "mimir/search/algorithms/iw/tuple_index_mapper.hpp"
#include "mimir/search/algorithms/strategies/layer_ordering_strategy.hpp"
#include "mimir/search/applicable_action_generators.hpp"
#include "mimir/search/axiom_evaluators.hpp"
#include "mimir/search/grounders.hpp"
#include "mimir/search/plan.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>

using namespace mimir::search;
using namespace mimir::formalism;

namespace mimir::tests
{

/// @brief Instantiate a lifted IW search
class LiftedIWPlanner
{
private:
    Problem m_problem;
    size_t m_arity;
    KPKCLiftedApplicableActionGeneratorImpl::EventHandler m_applicable_action_generator_event_handler;
    KPKCLiftedApplicableActionGenerator m_applicable_action_generator;
    KPKCLiftedAxiomEvaluatorImpl::EventHandler m_axiom_evaluator_event_handler;
    KPKCLiftedAxiomEvaluator m_axiom_evaluator;
    StateRepository m_state_repository;
    brfs::EventHandler m_brfs_event_handler;
    iw::EventHandler m_iw_event_handler;
    SearchContext m_search_context;

public:
    LiftedIWPlanner(const fs::path& domain_file, const fs::path& problem_file, size_t arity) :
        m_problem(ProblemImpl::create(domain_file, problem_file)),
        m_arity(arity),
        m_applicable_action_generator_event_handler(KPKCLiftedApplicableActionGeneratorImpl::DefaultEventHandlerImpl::create()),
        m_applicable_action_generator(KPKCLiftedApplicableActionGeneratorImpl::create(m_problem,
                                                                                      SearchContextImpl::LiftedOptions::KPKCOptions(),
                                                                                      m_applicable_action_generator_event_handler)),
        m_axiom_evaluator_event_handler(KPKCLiftedAxiomEvaluatorImpl::DefaultEventHandlerImpl::create()),
        m_axiom_evaluator(KPKCLiftedAxiomEvaluatorImpl::create(m_problem, m_axiom_evaluator_event_handler)),
        m_state_repository(StateRepositoryImpl::create(m_axiom_evaluator)),
        m_brfs_event_handler(brfs::DefaultEventHandlerImpl::create(m_problem)),
        m_iw_event_handler(iw::DefaultEventHandlerImpl::create(m_problem)),
        m_search_context(SearchContextImpl::create(m_problem, m_applicable_action_generator, m_state_repository))
    {
    }

    SearchResult find_solution()
    {
        auto iw_options = iw::Options();
        iw_options.max_arity = m_arity;
        iw_options.iw_event_handler = m_iw_event_handler;
        iw_options.brfs_event_handler = m_brfs_event_handler;

        return iw::find_solution(m_search_context, iw_options);
    }

    const iw::Statistics& get_iw_statistics() const { return m_iw_event_handler->get_statistics(); }

    const KPKCLiftedApplicableActionGeneratorImpl::Statistics& get_applicable_action_generator_statistics() const
    {
        return m_applicable_action_generator_event_handler->get_statistics();
    }

    const KPKCLiftedAxiomEvaluatorImpl::Statistics& get_axiom_evaluator_statistics() const { return m_axiom_evaluator_event_handler->get_statistics(); }
};

/// @brief Instantiate a grounded IW search
class GroundedIWPlanner
{
private:
    Problem m_problem;
    size_t m_arity;
    LiftedGrounder m_delete_relaxed_problem_explorator;
    GroundedApplicableActionGeneratorImpl::EventHandler m_applicable_action_generator_event_handler;
    GroundedApplicableActionGenerator m_applicable_action_generator;
    GroundedAxiomEvaluatorImpl::EventHandler m_axiom_evaluator_event_handler;
    GroundedAxiomEvaluator m_axiom_evaluator;
    StateRepository m_state_repository;
    brfs::EventHandler m_brfs_event_handler;
    iw::EventHandler m_iw_event_handler;
    SearchContext m_search_context;

public:
    GroundedIWPlanner(const fs::path& domain_file, const fs::path& problem_file, size_t arity) :
        m_problem(ProblemImpl::create(domain_file, problem_file)),
        m_arity(arity),
        m_delete_relaxed_problem_explorator(m_problem),
        m_applicable_action_generator_event_handler(GroundedApplicableActionGeneratorImpl::DefaultEventHandlerImpl::create()),
        m_applicable_action_generator(
            m_delete_relaxed_problem_explorator.create_grounded_applicable_action_generator(match_tree::Options(),
                                                                                            m_applicable_action_generator_event_handler)),
        m_axiom_evaluator_event_handler(GroundedAxiomEvaluatorImpl::DefaultEventHandlerImpl::create()),
        m_axiom_evaluator(m_delete_relaxed_problem_explorator.create_grounded_axiom_evaluator(match_tree::Options(), m_axiom_evaluator_event_handler)),
        m_state_repository(StateRepositoryImpl::create(m_axiom_evaluator)),
        m_brfs_event_handler(brfs::DefaultEventHandlerImpl::create(m_problem)),
        m_iw_event_handler(iw::DefaultEventHandlerImpl::create(m_problem)),
        m_search_context(SearchContextImpl::create(m_problem, m_applicable_action_generator, m_state_repository))
    {
    }

    SearchResult find_solution()
    {
        auto iw_options = iw::Options();
        iw_options.max_arity = m_arity;
        iw_options.iw_event_handler = m_iw_event_handler;
        iw_options.brfs_event_handler = m_brfs_event_handler;

        return iw::find_solution(m_search_context, iw_options);
    }

    const iw::Statistics& get_iw_statistics() const { return m_iw_event_handler->get_statistics(); }

    const GroundedApplicableActionGeneratorImpl::Statistics& get_applicable_action_generator_statistics() const
    {
        return m_applicable_action_generator_event_handler->get_statistics();
    }

    const GroundedAxiomEvaluatorImpl::Statistics& get_axiom_evaluator_statistics() const { return m_axiom_evaluator_event_handler->get_statistics(); }
};

static std::pair<GroundAtomList<FluentTag>, GroundAtom<FluentTag>> find_projective_iw1_candidate(const Problem& problem)
{
    auto fluent_atoms = GroundAtomList<FluentTag> {};
    for (const auto& atom : problem->get_repositories().get_ground_atoms<FluentTag>())
    {
        fluent_atoms.push_back(&atom);
    }

    for (const auto& target_atom : fluent_atoms)
    {
        if (target_atom->get_arity() <= 1)
        {
            continue;
        }

        auto covering_atoms_by_position = std::vector<GroundAtom<FluentTag>>(target_atom->get_arity(), nullptr);
        const auto& target_objects = target_atom->get_objects();

        for (const auto& candidate_atom : fluent_atoms)
        {
            if ((candidate_atom == target_atom) || (candidate_atom->get_predicate() != target_atom->get_predicate()))
            {
                continue;
            }

            const auto& candidate_objects = candidate_atom->get_objects();
            for (size_t position = 0; position < target_objects.size(); ++position)
            {
                if (!covering_atoms_by_position[position] && (candidate_objects[position] == target_objects[position]))
                {
                    covering_atoms_by_position[position] = candidate_atom;
                }
            }
        }

        if (!std::ranges::all_of(covering_atoms_by_position, [](const auto atom) { return atom != nullptr; }))
        {
            continue;
        }

        auto covering_atoms = GroundAtomList<FluentTag> {};
        for (const auto atom : covering_atoms_by_position)
        {
            if (std::ranges::find(covering_atoms, atom) == covering_atoms.end())
            {
                covering_atoms.push_back(atom);
            }
        }

        std::ranges::sort(covering_atoms, [](const auto lhs, const auto rhs) { return lhs->get_index() < rhs->get_index(); });

        return { covering_atoms, target_atom };
    }

    throw std::runtime_error("SearchAlgorithmsIWProjectiveArityOneNoveltyPruningStrategyTest: Could not find suitable projected-atom test candidate.");
}

static std::tuple<State, State, State, State, size_t> create_iw1_beam_novelty_test_states(const Problem& problem, const SearchContext& search_context)
{
    auto fluent_atoms = GroundAtomList<FluentTag> {};
    for (const auto& atom : problem->get_repositories().get_ground_atoms<FluentTag>())
    {
        fluent_atoms.push_back(&atom);
    }

    if (fluent_atoms.size() < 3)
    {
        throw std::runtime_error("SearchAlgorithmsIWBeamNoveltyTest: Requires at least three fluent ground atoms.");
    }

    auto& state_repository = *search_context->get_state_repository();
    const auto numeric_values = problem->get_initial_function_to_value<FluentTag>();

    auto parent_a_atoms = GroundAtomList<FluentTag> { fluent_atoms[0] };
    auto parent_b_atoms = GroundAtomList<FluentTag> { fluent_atoms[1] };
    auto succ_a_atoms = GroundAtomList<FluentTag> { fluent_atoms[0], fluent_atoms[2] };
    auto succ_b_atoms = GroundAtomList<FluentTag> { fluent_atoms[1], fluent_atoms[2] };

    const auto [parent_a, parent_a_metric_value] = state_repository.get_or_create_state(parent_a_atoms, numeric_values);
    const auto [parent_b, parent_b_metric_value] = state_repository.get_or_create_state(parent_b_atoms, numeric_values);
    const auto [succ_a, succ_a_metric_value] = state_repository.get_or_create_state(succ_a_atoms, numeric_values);
    const auto [succ_b, succ_b_metric_value] = state_repository.get_or_create_state(succ_b_atoms, numeric_values);

    [[maybe_unused]] const auto ignored_parent_a_metric_value = parent_a_metric_value;
    [[maybe_unused]] const auto ignored_parent_b_metric_value = parent_b_metric_value;
    [[maybe_unused]] const auto ignored_succ_a_metric_value = succ_a_metric_value;
    [[maybe_unused]] const auto ignored_succ_b_metric_value = succ_b_metric_value;

    return { parent_a, parent_b, succ_a, succ_b, fluent_atoms.size() };
}

TEST(MimirTests, SearchAlgorithmsIWSingleStateTupleIndexGeneratorWidth0Test)
{
    const int arity = 0;
    const int num_atoms = 3;

    const auto tuple_index_mapper = iw::TupleIndexMapper(arity, num_atoms);
    auto generator = iw::StateTupleIndexGenerator(&tuple_index_mapper);
    const auto atom_indices = iw::AtomIndexList({
        num_atoms,  // placeholder to generate tuples of size less than arity
    });

    auto iter = generator.begin(atom_indices);

    EXPECT_EQ("()", tuple_index_mapper.tuple_index_to_string(*(++iter)));

    EXPECT_EQ(++iter, generator.end());
}

TEST(MimirTests, SearchAlgorithmsIWSingleStateTupleIndexGeneratorWidth1Test)
{
    const int arity = 1;
    const int num_atoms = 3;

    const auto tuple_index_mapper = iw::TupleIndexMapper(arity, num_atoms);
    auto generator = iw::StateTupleIndexGenerator(&tuple_index_mapper);
    const auto atom_indices = iw::AtomIndexList({
        0,
        2,
        num_atoms,  // placeholder to generate tuples of size less than arity
    });

    auto iter = generator.begin(atom_indices);

    EXPECT_EQ("(0,)", tuple_index_mapper.tuple_index_to_string(*iter));
    EXPECT_EQ("(2,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("()", tuple_index_mapper.tuple_index_to_string(*(++iter)));

    EXPECT_EQ(++iter, generator.end());
}

TEST(MimirTests, SearchAlgorithmsIWSingleStateTupleIndexGeneratorWidth2Test)
{
    const int arity = 2;
    const int num_atoms = 3;

    const auto tuple_index_mapper = iw::TupleIndexMapper(arity, num_atoms);
    auto generator = iw::StateTupleIndexGenerator(&tuple_index_mapper);
    const auto atom_indices = iw::AtomIndexList({
        0,
        2,
        num_atoms,  // placeholder to generate tuples of size less than arity
    });

    auto iter = generator.begin(atom_indices);

    EXPECT_EQ("(0,2,)", tuple_index_mapper.tuple_index_to_string(*iter));
    EXPECT_EQ("(0,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(2,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("()", tuple_index_mapper.tuple_index_to_string(*(++iter)));

    EXPECT_EQ(++iter, generator.end());
}

TEST(MimirTests, SearchAlgorithmsIWStatePairTupleIndexGeneratorWidth1Test)
{
    const int arity = 1;
    const int num_atoms = 4;

    const auto tuple_index_mapper = iw::TupleIndexMapper(arity, num_atoms);
    auto generator = iw::StatePairTupleIndexGenerator(&tuple_index_mapper);
    const auto atom_indices = iw::AtomIndexList({
        0,
        2,
        num_atoms,  // placeholder to generate tuples of size less than arity
    });
    const auto add_atom_indices = iw::AtomIndexList({
        1,
        3,
    });

    auto iter = generator.begin(atom_indices, add_atom_indices);

    EXPECT_EQ("(1,)", tuple_index_mapper.tuple_index_to_string(*iter));
    EXPECT_EQ("(3,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ(++iter, generator.end());
}

TEST(MimirTests, SearchAlgorithmsIWStatePairTupleIndexGeneratorWidth2Test1)
{
    const int arity = 2;
    const int num_atoms = 4;

    const auto tuple_index_mapper = iw::TupleIndexMapper(arity, num_atoms);
    auto generator = iw::StatePairTupleIndexGenerator(&tuple_index_mapper);
    const auto atom_indices = iw::AtomIndexList({
        0,
        2,
        num_atoms,  // placeholder to generate tuples of size less than arity
    });
    const auto add_atom_indices = iw::AtomIndexList({
        1,
        3,
    });

    auto iter = generator.begin(atom_indices, add_atom_indices);

    EXPECT_EQ("(1,2,)", tuple_index_mapper.tuple_index_to_string(*iter));
    EXPECT_EQ("(1,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(3,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(0,1,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(0,3,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(2,3,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(1,3,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));

    EXPECT_EQ(++iter, generator.end());
}

TEST(MimirTests, SearchAlgorithmsIWStatePairTupleIndexGeneratorWidth2Test2)
{
    const int arity = 3;
    const int num_atoms = 64;

    const auto tuple_index_mapper = iw::TupleIndexMapper(arity, num_atoms);
    auto generator = iw::StatePairTupleIndexGenerator(&tuple_index_mapper);
    const auto atom_indices = iw::AtomIndexList({
        0,
        2,
        3,
        4,
        num_atoms,  // placeholder to generate tuples of size less than arity
    });
    const auto add_atom_indices = iw::AtomIndexList({
        6,
    });

    auto iter = generator.begin(atom_indices, add_atom_indices);

    EXPECT_EQ("(6,)", tuple_index_mapper.tuple_index_to_string(*iter));
    EXPECT_EQ("(0,6,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(2,6,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(3,6,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(4,6,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(0,2,6,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(0,3,6,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(0,4,6,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(2,3,6,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(2,4,6,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(3,4,6,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));

    EXPECT_EQ(++iter, generator.end());
}

TEST(MimirTests, SearchAlgorithmsIWStatePairTupleIndexGeneratorWidth2Test3)
{
    const int arity = 2;
    const int num_atoms = 64;

    const auto tuple_index_mapper = iw::TupleIndexMapper(arity, num_atoms);
    auto generator = iw::StatePairTupleIndexGenerator(&tuple_index_mapper);
    const auto atom_indices = iw::AtomIndexList({
        0,
        1,
        num_atoms,  // placeholder to generate tuples of size less than arity
    });
    const auto add_atom_indices = iw::AtomIndexList({
        2,
        3,
    });

    auto iter = generator.begin(atom_indices, add_atom_indices);

    EXPECT_EQ("(2,)", tuple_index_mapper.tuple_index_to_string(*iter));
    EXPECT_EQ("(3,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(0,2,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(0,3,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(1,2,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(1,3,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));
    EXPECT_EQ("(2,3,)", tuple_index_mapper.tuple_index_to_string(*(++iter)));

    EXPECT_EQ(++iter, generator.end());
}

TEST(MimirTests, SearchAlgorithmsIWProjectiveArityOneNoveltyPruningStrategyDepthOneNovelByDefaultTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "gripper/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl");
    const auto problem = ProblemImpl::create(domain_file, problem_file);

    const auto search_context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));
    auto& state_repository = *search_context->get_state_repository();

    auto [covering_atoms, target_atom] = find_projective_iw1_candidate(problem);
    const auto numeric_values = problem->get_initial_function_to_value<FluentTag>();

    const auto [state, state_metric_value] = state_repository.get_or_create_state(covering_atoms, numeric_values);
    [[maybe_unused]] const auto ignored_state_metric_value = state_metric_value;

    covering_atoms.push_back(target_atom);
    std::sort(covering_atoms.begin(), covering_atoms.end(), [](const auto lhs, const auto rhs) { return lhs->get_index() < rhs->get_index(); });
    covering_atoms.erase(std::unique(covering_atoms.begin(), covering_atoms.end()), covering_atoms.end());

    const auto [succ_state, succ_state_metric_value] = state_repository.get_or_create_state(covering_atoms, numeric_values);
    [[maybe_unused]] const auto ignored_succ_state_metric_value = succ_state_metric_value;

    const auto projective_iw1 = iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(problem);

    EXPECT_FALSE(projective_iw1->test_prune_initial_state(state));
    EXPECT_FALSE(projective_iw1->test_prune_successor_state(state, succ_state, true));
}

TEST(MimirTests, SearchAlgorithmsIWProjectiveArityOneNoveltyPruningStrategyOptOutDepthOneNoveltyTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "gripper/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl");
    const auto problem = ProblemImpl::create(domain_file, problem_file);

    const auto search_context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));
    auto& state_repository = *search_context->get_state_repository();

    auto [covering_atoms, target_atom] = find_projective_iw1_candidate(problem);
    const auto numeric_values = problem->get_initial_function_to_value<FluentTag>();

    const auto [state, state_metric_value] = state_repository.get_or_create_state(covering_atoms, numeric_values);
    [[maybe_unused]] const auto ignored_state_metric_value = state_metric_value;

    covering_atoms.push_back(target_atom);
    std::sort(covering_atoms.begin(), covering_atoms.end(), [](const auto lhs, const auto rhs) { return lhs->get_index() < rhs->get_index(); });
    covering_atoms.erase(std::unique(covering_atoms.begin(), covering_atoms.end()), covering_atoms.end());

    const auto [succ_state, succ_state_metric_value] = state_repository.get_or_create_state(covering_atoms, numeric_values);
    [[maybe_unused]] const auto ignored_succ_state_metric_value = succ_state_metric_value;

    const auto projective_iw1 = iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(problem, false, false);
    const auto iw1 = iw::ArityKNoveltyPruningStrategyImpl::create(1, iw::INITIAL_TABLE_ATOMS);

    EXPECT_FALSE(projective_iw1->test_prune_initial_state(state));
    EXPECT_FALSE(iw1->test_prune_initial_state(state));

    EXPECT_TRUE(projective_iw1->test_prune_successor_state(state, succ_state, true));
    EXPECT_FALSE(iw1->test_prune_successor_state(state, succ_state, true));
}

TEST(MimirTests, SearchAlgorithmsIWProjectiveArityOneNoveltyPruningStrategyTypedProjectionRequiresTypingTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "gripper/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl");
    const auto problem = ProblemImpl::create(domain_file, problem_file);

    EXPECT_THROW(iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(problem, true), std::runtime_error);
}

TEST(MimirTests, SearchAlgorithmsIWProjectiveArityOneNoveltyPruningStrategyTypedProjectionTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "driverlog/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "driverlog/test_problem.pddl");
    const auto problem = ProblemImpl::create(domain_file, problem_file);

    const auto search_context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));
    auto& state_repository = *search_context->get_state_repository();

    auto [covering_atoms, target_atom] = find_projective_iw1_candidate(problem);
    const auto numeric_values = problem->get_initial_function_to_value<FluentTag>();

    const auto [state, state_metric_value] = state_repository.get_or_create_state(covering_atoms, numeric_values);
    [[maybe_unused]] const auto ignored_state_metric_value = state_metric_value;

    covering_atoms.push_back(target_atom);
    std::sort(covering_atoms.begin(), covering_atoms.end(), [](const auto lhs, const auto rhs) { return lhs->get_index() < rhs->get_index(); });
    covering_atoms.erase(std::unique(covering_atoms.begin(), covering_atoms.end()), covering_atoms.end());

    const auto [succ_state, succ_state_metric_value] = state_repository.get_or_create_state(covering_atoms, numeric_values);
    [[maybe_unused]] const auto ignored_succ_state_metric_value = succ_state_metric_value;

    const auto typed_projective_iw1 = iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(problem, true, false);

    EXPECT_FALSE(typed_projective_iw1->test_prune_initial_state(state));
    EXPECT_TRUE(typed_projective_iw1->test_prune_successor_state(state, succ_state, true));
}

TEST(MimirTests, SearchAlgorithmsIWProjectiveArityOneNoveltyPruningStrategyKeepGoalNonUnaryAtomsTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "driverlog/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "driverlog/test_problem.pddl");
    const auto problem = ProblemImpl::create(domain_file, problem_file);

    const auto search_context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));
    auto& state_repository = *search_context->get_state_repository();

    const auto at_predicate = problem->get_domain()->get_predicate<FluentTag>("at");
    const auto package1 = problem->get_problem_or_domain_object("package1");
    const auto truck1 = problem->get_problem_or_domain_object("truck1");
    const auto s0 = problem->get_problem_or_domain_object("s0");
    const auto s1 = problem->get_problem_or_domain_object("s1");
    auto covering_atoms = GroundAtomList<FluentTag> {
        problem->get_or_create_ground_atom<FluentTag>(at_predicate, ObjectList { package1, s1 }),
        problem->get_or_create_ground_atom<FluentTag>(at_predicate, ObjectList { truck1, s0 }),
    };
    const auto target_atom = problem->get_or_create_ground_atom<FluentTag>(at_predicate, ObjectList { package1, s0 });
    const auto numeric_values = problem->get_initial_function_to_value<FluentTag>();

    const auto [state, state_metric_value] = state_repository.get_or_create_state(covering_atoms, numeric_values);
    [[maybe_unused]] const auto ignored_state_metric_value = state_metric_value;

    covering_atoms.push_back(target_atom);
    std::sort(covering_atoms.begin(), covering_atoms.end(), [](const auto lhs, const auto rhs) { return lhs->get_index() < rhs->get_index(); });
    covering_atoms.erase(std::unique(covering_atoms.begin(), covering_atoms.end()), covering_atoms.end());

    const auto [succ_state, succ_state_metric_value] = state_repository.get_or_create_state(covering_atoms, numeric_values);
    [[maybe_unused]] const auto ignored_succ_state_metric_value = succ_state_metric_value;

    const auto projective_iw1 = iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(problem, false, false, false);
    const auto goal_aware_projective_iw1 = iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(problem, false, false, true);

    EXPECT_FALSE(projective_iw1->test_prune_initial_state(state));
    EXPECT_FALSE(goal_aware_projective_iw1->test_prune_initial_state(state));

    EXPECT_TRUE(projective_iw1->test_prune_successor_state(state, succ_state, true));
    EXPECT_FALSE(goal_aware_projective_iw1->test_prune_successor_state(state, succ_state, true));
}

TEST(MimirTests, SearchAlgorithmsIWArityOneBeamAllTestedContaminatesNoveltyTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "gripper/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl");
    const auto problem = ProblemImpl::create(domain_file, problem_file);
    const auto search_context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));

    const auto [parent_a, parent_b, succ_a, succ_b, num_atoms] = create_iw1_beam_novelty_test_states(problem, search_context);
    const auto iw1 = iw::ArityKNoveltyPruningStrategyImpl::create(1, num_atoms);

    EXPECT_FALSE(iw1->test_prune_initial_state(parent_a));
    EXPECT_FALSE(iw1->test_prune_successor_state(parent_a, parent_b, true));
    EXPECT_FALSE(iw1->test_prune_successor_state_for_beam_selection(parent_a, succ_a, true, BeamNoveltyMode::ALL_TESTED));
    EXPECT_TRUE(iw1->test_prune_successor_state_for_beam_selection(parent_b, succ_b, true, BeamNoveltyMode::ALL_TESTED));
}

TEST(MimirTests, SearchAlgorithmsIWArityZeroBeamSurvivorsOnlySupportedTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "gripper/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "gripper/p-2-0.pddl");
    const auto problem = ProblemImpl::create(domain_file, problem_file);
    const auto search_context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));

    auto options = iw::Options();
    options.max_arity = 0;
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
    options.beam_width = 2;
    options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;

    const auto result = iw::find_solution(search_context, options);
    EXPECT_EQ(result.status, SearchStatus::FAILED);
}

TEST(MimirTests, SearchAlgorithmsIWArityOneBeamSurvivorsOnlyDoesNotContaminateNoveltyTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "gripper/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl");
    const auto problem = ProblemImpl::create(domain_file, problem_file);
    const auto search_context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));

    const auto [parent_a, parent_b, succ_a, succ_b, num_atoms] = create_iw1_beam_novelty_test_states(problem, search_context);
    const auto iw1 = iw::ArityKNoveltyPruningStrategyImpl::create(1, num_atoms);

    EXPECT_FALSE(iw1->test_prune_initial_state(parent_a));
    EXPECT_FALSE(iw1->test_prune_successor_state(parent_a, parent_b, true));
    EXPECT_FALSE(iw1->test_prune_successor_state_for_beam_selection(parent_a, succ_a, true, BeamNoveltyMode::SURVIVORS_ONLY));
    EXPECT_FALSE(iw1->test_prune_successor_state_for_beam_selection(parent_b, succ_b, true, BeamNoveltyMode::SURVIVORS_ONLY));

    iw1->on_begin_beam_replay(BeamNoveltyMode::SURVIVORS_ONLY);
    EXPECT_FALSE(iw1->test_prune_successor_state_for_beam_replay(parent_b, succ_b, true, BeamNoveltyMode::SURVIVORS_ONLY));
    iw1->on_end_beam_replay(BeamNoveltyMode::SURVIVORS_ONLY);
}

TEST(MimirTests, SearchAlgorithmsIWArityOneBeamSurvivorsOnlyReplayCanReduceBeamWidthTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "gripper/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl");
    const auto problem = ProblemImpl::create(domain_file, problem_file);
    const auto search_context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));

    const auto [parent_a, parent_b, succ_a, succ_b, num_atoms] = create_iw1_beam_novelty_test_states(problem, search_context);
    const auto iw1 = iw::ArityKNoveltyPruningStrategyImpl::create(1, num_atoms);

    EXPECT_FALSE(iw1->test_prune_initial_state(parent_a));
    EXPECT_FALSE(iw1->test_prune_successor_state(parent_a, parent_b, true));
    EXPECT_FALSE(iw1->test_prune_successor_state_for_beam_selection(parent_a, succ_a, true, BeamNoveltyMode::SURVIVORS_ONLY));
    EXPECT_FALSE(iw1->test_prune_successor_state_for_beam_selection(parent_b, succ_b, true, BeamNoveltyMode::SURVIVORS_ONLY));

    iw1->on_begin_beam_replay(BeamNoveltyMode::SURVIVORS_ONLY);
    EXPECT_FALSE(iw1->test_prune_successor_state_for_beam_replay(parent_a, succ_a, true, BeamNoveltyMode::SURVIVORS_ONLY));
    EXPECT_TRUE(iw1->test_prune_successor_state_for_beam_replay(parent_b, succ_b, true, BeamNoveltyMode::SURVIVORS_ONLY));
    iw1->on_end_beam_replay(BeamNoveltyMode::SURVIVORS_ONLY);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Classical planning
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Delivery
 */

TEST(MimirTests, SearchAlgorithmsIWGroundedDeliveryTest)
{
    auto iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"), 3);
    const auto result = iw.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& iw_statistics = iw.get_iw_statistics();

    EXPECT_EQ(iw_statistics.get_brfs_statistics_by_arity().back().get_num_generated_until_g_value().back(), 18);
    EXPECT_EQ(iw_statistics.get_brfs_statistics_by_arity().back().get_num_expanded_until_g_value().back(), 7);
    EXPECT_EQ(iw_statistics.get_effective_width(), 2);
}

TEST(MimirTests, SearchAlgorithmsIWLiftedDeliveryTest)
{
    auto iw = LiftedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"), 3);
    const auto result = iw.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& applicable_action_generator_statistics = iw.get_applicable_action_generator_statistics();
    const auto& axiom_evaluator_statistics = iw.get_axiom_evaluator_statistics();

    EXPECT_EQ(applicable_action_generator_statistics.get_num_ground_action_cache_hits_per_search_layer().back(), 25);
    EXPECT_EQ(applicable_action_generator_statistics.get_num_ground_action_cache_misses_per_search_layer().back(), 12);

    EXPECT_EQ(axiom_evaluator_statistics.get_num_ground_axiom_cache_hits_per_search_layer().back(), 0);
    EXPECT_EQ(axiom_evaluator_statistics.get_num_ground_axiom_cache_misses_per_search_layer().back(), 0);

    const auto& iw_statistics = iw.get_iw_statistics();

    EXPECT_EQ(iw_statistics.get_brfs_statistics_by_arity().back().get_num_generated_until_g_value().back(), 18);
    EXPECT_EQ(iw_statistics.get_brfs_statistics_by_arity().back().get_num_expanded_until_g_value().back(), 7);
    EXPECT_EQ(iw_statistics.get_effective_width(), 2);
}

/**
 * Miconic-fulladl
 */

TEST(MimirTests, SearchAlgorithmsIWGroundedMiconicFullAdlTest)
{
    auto iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "miconic-fulladl/domain.pddl"),
                                fs::path(std::string(DATA_DIR) + "miconic-fulladl/test_problem.pddl"),
                                3);
    const auto result = iw.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 7);

    const auto& iw_statistics = iw.get_iw_statistics();

    EXPECT_EQ(iw_statistics.get_brfs_statistics_by_arity().back().get_num_generated_until_g_value().back(), 69);
    EXPECT_EQ(iw_statistics.get_brfs_statistics_by_arity().back().get_num_expanded_until_g_value().back(), 27);
    EXPECT_EQ(iw_statistics.get_effective_width(), 2);
}

TEST(MimirTests, SearchAlgorithmsIWLiftedMiconicFullAdlTest)
{
    auto iw = LiftedIWPlanner(fs::path(std::string(DATA_DIR) + "miconic-fulladl/domain.pddl"),
                              fs::path(std::string(DATA_DIR) + "miconic-fulladl/test_problem.pddl"),
                              3);
    const auto result = iw.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 7);

    const auto& applicable_action_generator_statistics = iw.get_applicable_action_generator_statistics();
    const auto& axiom_evaluator_statistics = iw.get_axiom_evaluator_statistics();

    EXPECT_EQ(applicable_action_generator_statistics.get_num_ground_action_cache_hits_per_search_layer().back(), 89);
    EXPECT_EQ(applicable_action_generator_statistics.get_num_ground_action_cache_misses_per_search_layer().back(), 10);

    EXPECT_EQ(axiom_evaluator_statistics.get_num_ground_axiom_cache_hits_per_search_layer().back(), 345);
    EXPECT_EQ(axiom_evaluator_statistics.get_num_ground_axiom_cache_misses_per_search_layer().back(), 15);

    const auto& iw_statistics = iw.get_iw_statistics();

    EXPECT_EQ(iw_statistics.get_brfs_statistics_by_arity().back().get_num_generated_until_g_value().back(), 69);
    EXPECT_EQ(iw_statistics.get_brfs_statistics_by_arity().back().get_num_expanded_until_g_value().back(), 27);
    EXPECT_EQ(iw_statistics.get_effective_width(), 2);
}

}
