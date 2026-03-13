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
#include "mimir/search/applicability.hpp"
#include "mimir/search/algorithms.hpp"
#include "mimir/search/algorithms/iw/event_handlers.hpp"
#include "mimir/search/algorithms/iw/pruning_strategy.hpp"
#include "mimir/search/algorithms/strategies/goal_strategy.hpp"
#include "mimir/search/algorithms/iw/tuple_index_generators.hpp"
#include "mimir/search/algorithms/iw/tuple_index_mapper.hpp"
#include "mimir/search/algorithms/strategies/layer_ordering_strategy.hpp"
#include "mimir/search/applicable_action_generators.hpp"
#include "mimir/search/axiom_evaluators.hpp"
#include "mimir/search/grounders.hpp"
#include "mimir/search/plan.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"
#include "mimir/datasets/state_space.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <stdexcept>
#include <vector>

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
    LiftedIWPlanner(const fs::path& domain_file,
                    const fs::path& problem_file,
                    size_t arity,
                    SearchContextImpl::SymmetryPruning symmetry_pruning = SearchContextImpl::SymmetryPruning::OFF) :
        m_problem(ProblemImpl::create(domain_file, problem_file)),
        m_arity(arity),
        m_applicable_action_generator_event_handler(KPKCLiftedApplicableActionGeneratorImpl::DefaultEventHandlerImpl::create()),
        m_applicable_action_generator(KPKCLiftedApplicableActionGeneratorImpl::create(m_problem,
                                                                                      SearchContextImpl::LiftedOptions::KPKCOptions(symmetry_pruning),
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
    const Problem& get_problem() const { return m_problem; }
    const SearchContext& get_search_context() const { return m_search_context; }
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
    const Problem& get_problem() const { return m_problem; }
    const SearchContext& get_search_context() const { return m_search_context; }
};

class ConstantScoringLayerOrderingStrategy : public ILayerOrderingStrategy
{
public:
    bool supports_eager_scoring() const override { return true; }

    ContinuousCost score_state(const State& state, DiscreteCost g_value) const override
    {
        [[maybe_unused]] const auto& ignored_state = state;
        [[maybe_unused]] const auto ignored_g_value = g_value;
        return 1.0;
    }

    bool prefer_higher_scores() const override { return true; }

    void order_layer(StateList& states, DiscreteCost g_value) override
    {
        [[maybe_unused]] auto& ignored_states = states;
        [[maybe_unused]] const auto ignored_g_value = g_value;
    }
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

static std::vector<std::string> get_plan_action_signatures(const Plan& plan)
{
    auto signatures = std::vector<std::string> {};
    signatures.reserve(plan.get_actions().size());

    for (const auto action : plan.get_actions())
    {
        auto signature = action->get_action()->get_name() + "(";
        bool first = true;

        for (const auto object : action->get_objects())
        {
            if (!first)
            {
                signature += ",";
            }
            first = false;
            signature += object->get_name();
        }

        signature += ")";
        signatures.push_back(std::move(signature));
    }

    return signatures;
}

static void expect_plan_reaches_goal(const SearchContext& search_context, const SearchResult& result)
{
    ASSERT_TRUE(result.plan.has_value());

    auto& state_repository = *search_context->get_state_repository();
    const auto goal_strategy = ProblemGoalStrategyImpl::create(search_context->get_problem());
    ASSERT_TRUE(goal_strategy->test_static_goal());

    const auto [initial_state, initial_metric_value] = state_repository.get_or_create_initial_state();
    auto state = initial_state;
    auto state_metric_value = initial_metric_value;

    for (const auto action : result.plan->get_actions())
    {
        ASSERT_TRUE(is_applicable(action, state));

        const auto successor = state_repository.get_or_create_successor_state(state, action, state_metric_value);
        state = successor.first;
        state_metric_value = successor.second;
    }

    EXPECT_TRUE(goal_strategy->test_dynamic_goal(state));
    if (result.goal_state.has_value())
    {
        EXPECT_EQ(state.get_index(), result.goal_state->get_index());
    }
}

struct IWRunTrace
{
    SearchStatus status;
    std::optional<Index> goal_state_index;
    std::vector<std::string> plan_action_signatures;
    int effective_width;
    std::vector<std::vector<uint64_t>> num_generated_until_g_value_by_arity;
    std::vector<std::vector<uint64_t>> num_expanded_until_g_value_by_arity;
    std::vector<std::vector<uint64_t>> num_pruned_until_g_value_by_arity;
};

static IWRunTrace make_iw_run_trace(const SearchResult& result, const iw::Statistics& statistics)
{
    auto num_generated_until_g_value_by_arity = std::vector<std::vector<uint64_t>> {};
    auto num_expanded_until_g_value_by_arity = std::vector<std::vector<uint64_t>> {};
    auto num_pruned_until_g_value_by_arity = std::vector<std::vector<uint64_t>> {};

    for (const auto& brfs_statistics : statistics.get_brfs_statistics_by_arity())
    {
        num_generated_until_g_value_by_arity.push_back(brfs_statistics.get_num_generated_until_g_value());
        num_expanded_until_g_value_by_arity.push_back(brfs_statistics.get_num_expanded_until_g_value());
        num_pruned_until_g_value_by_arity.push_back(brfs_statistics.get_num_pruned_until_g_value());
    }

    return IWRunTrace {
        result.status,
        result.goal_state.has_value() ? std::make_optional(result.goal_state->get_index()) : std::nullopt,
        result.plan.has_value() ? get_plan_action_signatures(*result.plan) : std::vector<std::string> {},
        statistics.get_effective_width(),
        std::move(num_generated_until_g_value_by_arity),
        std::move(num_expanded_until_g_value_by_arity),
        std::move(num_pruned_until_g_value_by_arity),
    };
}

static void expect_iw_run_traces_match(const IWRunTrace& lhs, const IWRunTrace& rhs)
{
    EXPECT_EQ(lhs.status, rhs.status);
    EXPECT_EQ(lhs.goal_state_index, rhs.goal_state_index);
    EXPECT_EQ(lhs.plan_action_signatures, rhs.plan_action_signatures);
    EXPECT_EQ(lhs.effective_width, rhs.effective_width);
    EXPECT_EQ(lhs.num_generated_until_g_value_by_arity, rhs.num_generated_until_g_value_by_arity);
    EXPECT_EQ(lhs.num_expanded_until_g_value_by_arity, rhs.num_expanded_until_g_value_by_arity);
    EXPECT_EQ(lhs.num_pruned_until_g_value_by_arity, rhs.num_pruned_until_g_value_by_arity);
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

TEST(MimirTests, SearchAlgorithmsIWParallelBeamMatchesSerialTest)
{
    auto serial_iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"), 2);
    auto serial_options = iw::Options();
    serial_options.max_arity = 2;
    serial_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(serial_iw.get_problem());
    serial_options.beam_width = 4;
    serial_options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
    const auto serial_result = iw::find_solution(serial_iw.get_search_context(), serial_options);

    auto parallel_iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"), 2);
    auto parallel_options = serial_options;
    parallel_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(parallel_iw.get_problem());
    parallel_options.parallel_beam_num_threads = 2;
    const auto parallel_result = iw::find_solution(parallel_iw.get_search_context(), parallel_options);

    ASSERT_EQ(parallel_result.status, serial_result.status);
    ASSERT_EQ(parallel_result.plan.has_value(), serial_result.plan.has_value());
    ASSERT_EQ(parallel_result.goal_state.has_value(), serial_result.goal_state.has_value());

    if (serial_result.plan.has_value())
    {
        const auto& serial_actions = serial_result.plan->get_actions();
        const auto& parallel_actions = parallel_result.plan->get_actions();
        ASSERT_EQ(parallel_actions.size(), serial_actions.size());
        for (size_t i = 0; i < serial_actions.size(); ++i)
        {
            EXPECT_EQ(parallel_actions[i]->get_action()->get_name(), serial_actions[i]->get_action()->get_name());
            ASSERT_EQ(parallel_actions[i]->get_objects().size(), serial_actions[i]->get_objects().size());
            for (size_t j = 0; j < serial_actions[i]->get_objects().size(); ++j)
            {
                EXPECT_EQ(parallel_actions[i]->get_objects()[j]->get_name(), serial_actions[i]->get_objects()[j]->get_name());
            }
        }
    }
}

TEST(MimirTests, SearchAlgorithmsIWParallelAllTestedBeamMatchesSerialTest)
{
    auto serial_iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem2.pddl"), 2);
    auto serial_options = iw::Options();
    serial_options.max_arity = 2;
    serial_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(serial_iw.get_problem());
    serial_options.beam_width = 128;
    serial_options.beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
    const auto serial_result = iw::find_solution(serial_iw.get_search_context(), serial_options);

    auto parallel_iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem2.pddl"), 2);
    auto parallel_options = serial_options;
    parallel_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(parallel_iw.get_problem());
    parallel_options.parallel_beam_num_threads = 2;
    const auto parallel_result = iw::find_solution(parallel_iw.get_search_context(), parallel_options);

    ASSERT_EQ(parallel_result.status, serial_result.status);
    ASSERT_EQ(parallel_result.plan.has_value(), serial_result.plan.has_value());
    ASSERT_EQ(parallel_result.goal_state.has_value(), serial_result.goal_state.has_value());

    if (serial_result.plan.has_value())
    {
        const auto& serial_actions = serial_result.plan->get_actions();
        const auto& parallel_actions = parallel_result.plan->get_actions();
        ASSERT_EQ(parallel_actions.size(), serial_actions.size());
        for (size_t i = 0; i < serial_actions.size(); ++i)
        {
            EXPECT_EQ(parallel_actions[i]->get_action()->get_name(), serial_actions[i]->get_action()->get_name());
            ASSERT_EQ(parallel_actions[i]->get_objects().size(), serial_actions[i]->get_objects().size());
            for (size_t j = 0; j < serial_actions[i]->get_objects().size(); ++j)
            {
                EXPECT_EQ(parallel_actions[i]->get_objects()[j]->get_name(), serial_actions[i]->get_objects()[j]->get_name());
            }
        }
    }
}

TEST(MimirTests, SearchAlgorithmsIWParallelBeamRepeatedMatchesSerialDeliveryTest)
{
    auto run = [](const fs::path& problem_file, BeamNoveltyMode beam_novelty_mode, uint32_t beam_width, uint32_t parallel_threads)
    {
        auto iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), problem_file, 2);
        auto iw_event_handler = iw::DefaultEventHandlerImpl::create(iw.get_problem());

        auto options = iw::Options();
        options.max_arity = 2;
        options.iw_event_handler = iw_event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(iw.get_problem());
        options.beam_width = beam_width;
        options.beam_novelty_mode = beam_novelty_mode;
        options.parallel_beam_num_threads = parallel_threads;

        const auto result = iw::find_solution(iw.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(iw.get_search_context(), result);
        }

        return make_iw_run_trace(result, iw_event_handler->get_statistics());
    };

    struct TestCase
    {
        fs::path problem_file;
        BeamNoveltyMode beam_novelty_mode;
        uint32_t beam_width;
        const char* label;
    };

    for (const auto& test_case : std::array<TestCase, 2> { TestCase { fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"),
                                                                         BeamNoveltyMode::SURVIVORS_ONLY,
                                                                         4,
                                                                         "survivors_only" },
                                                            TestCase { fs::path(std::string(DATA_DIR) + "delivery/test_problem2.pddl"),
                                                                         BeamNoveltyMode::ALL_TESTED,
                                                                         128,
                                                                         "all_tested" } })
    {
        SCOPED_TRACE(test_case.label);

        const auto serial_trace = run(test_case.problem_file, test_case.beam_novelty_mode, test_case.beam_width, 1);

        for (int repetition = 0; repetition < 8; ++repetition)
        {
            SCOPED_TRACE(repetition);

            for (const auto parallel_threads : { 2u, 4u, 8u })
            {
                SCOPED_TRACE(parallel_threads);
                expect_iw_run_traces_match(run(test_case.problem_file, test_case.beam_novelty_mode, test_case.beam_width, parallel_threads),
                                           serial_trace);
            }
        }
    }
}

TEST(MimirTests, SearchAlgorithmsIWParallelBeamCustomChunkSizeMatchesSerialTest)
{
    auto run = [](uint32_t chunk_size)
    {
        auto iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem2.pddl"), 2);
        auto iw_event_handler = iw::DefaultEventHandlerImpl::create(iw.get_problem());
        auto options = iw::Options();
        options.max_arity = 2;
        options.iw_event_handler = iw_event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(iw.get_problem());
        options.beam_width = 128;
        options.beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
        if (chunk_size > 0)
        {
            options.parallel_beam_num_threads = 2;
            options.parallel_beam_chunk_size = chunk_size;
        }

        const auto result = iw::find_solution(iw.get_search_context(), options);
        return std::make_pair(make_iw_run_trace(result, iw_event_handler->get_statistics()), iw_event_handler->get_statistics());
    };

    const auto [serial_trace, serial_statistics] = run(0);
    [[maybe_unused]] const auto ignored_serial_statistics = serial_statistics;

    for (const auto chunk_size : { 1u, 4096u })
    {
        SCOPED_TRACE(chunk_size);
        const auto [parallel_trace, parallel_statistics] = run(chunk_size);
        expect_iw_run_traces_match(parallel_trace, serial_trace);

        uint64_t chunk_flushes = 0;
        uint64_t max_chunk_size = 0;
        for (const auto& brfs_statistics : parallel_statistics.get_brfs_statistics_by_arity())
        {
            chunk_flushes += brfs_statistics.get_num_parallel_beam_chunk_flushes();
            max_chunk_size = std::max(max_chunk_size, brfs_statistics.get_max_parallel_beam_chunk_size());
        }
        EXPECT_GT(chunk_flushes, 0);
        EXPECT_LE(max_chunk_size, chunk_size);
    }
}

TEST(MimirTests, SearchAlgorithmsIWRelaxedSurvivorsOnlyBeamRequiresSurvivorsOnlyMode)
{
    auto iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"),
                                2);

    auto options = iw::Options();
    options.max_arity = 2;
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(iw.get_problem());
    options.beam_width = 8;
    options.beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
    options.parallel_beam_num_threads = 2;
    options.relaxed_survivors_only_beam = true;

    EXPECT_THROW(iw::find_solution(iw.get_search_context(), options), std::invalid_argument);
}

TEST(MimirTests, SearchAlgorithmsIWRelaxedSurvivorsOnlyBeamWideBeamMatchesDeterministicTest)
{
    auto run = [](bool relaxed_survivors_only_beam)
    {
        auto iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                    fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"),
                                    2);
        auto iw_event_handler = iw::DefaultEventHandlerImpl::create(iw.get_problem());

        const auto state_space_result = datasets::StateSpaceImpl::create(iw.get_search_context());
        EXPECT_TRUE(state_space_result.has_value());
        if (!state_space_result.has_value())
        {
            return IWRunTrace {};
        }

        auto options = iw::Options();
        options.max_arity = 2;
        options.iw_event_handler = iw_event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(iw.get_problem());
        options.beam_width = static_cast<uint32_t>(state_space_result->first->get_graph().get_num_vertices());
        options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
        options.parallel_beam_num_threads = 4;
        options.relaxed_survivors_only_beam = relaxed_survivors_only_beam;

        const auto result = iw::find_solution(iw.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(iw.get_search_context(), result);
        }

        return make_iw_run_trace(result, iw_event_handler->get_statistics());
    };

    const auto relaxed_trace = run(true);
    const auto deterministic_trace = run(false);

    EXPECT_EQ(relaxed_trace.status, deterministic_trace.status);
    EXPECT_EQ(relaxed_trace.goal_state_index, deterministic_trace.goal_state_index);
    EXPECT_EQ(relaxed_trace.plan_action_signatures, deterministic_trace.plan_action_signatures);
    EXPECT_EQ(relaxed_trace.effective_width, deterministic_trace.effective_width);
    EXPECT_EQ(relaxed_trace.num_expanded_until_g_value_by_arity, deterministic_trace.num_expanded_until_g_value_by_arity);
    EXPECT_EQ(relaxed_trace.num_pruned_until_g_value_by_arity, deterministic_trace.num_pruned_until_g_value_by_arity);
}

TEST(MimirTests, SearchAlgorithmsIWParallelBeamChunkedScheduleMatchesSerialTest)
{
    auto run = [](BeamNoveltyMode beam_novelty_mode, uint32_t parallel_threads)
    {
        auto iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "schedule/domain.pddl"),
                                    fs::path(std::string(DATA_DIR) + "schedule/test_problem.pddl"),
                                    5);
        auto iw_event_handler = iw::DefaultEventHandlerImpl::create(iw.get_problem());

        auto options = iw::Options();
        options.max_arity = 5;
        options.iw_event_handler = iw_event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(iw.get_problem());
        options.beam_width = 256;
        options.beam_novelty_mode = beam_novelty_mode;
        options.parallel_beam_num_threads = parallel_threads;

        const auto result = iw::find_solution(iw.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(iw.get_search_context(), result);
        }

        return make_iw_run_trace(result, iw_event_handler->get_statistics());
    };

    for (const auto beam_novelty_mode : { BeamNoveltyMode::ALL_TESTED, BeamNoveltyMode::SURVIVORS_ONLY })
    {
        SCOPED_TRACE(beam_novelty_mode == BeamNoveltyMode::ALL_TESTED ? "all_tested" : "survivors_only");

        const auto serial_trace = run(beam_novelty_mode, 1);

        for (const auto parallel_threads : { 2u, 4u })
        {
            SCOPED_TRACE(parallel_threads);

            const auto parallel_trace = run(beam_novelty_mode, parallel_threads);
            EXPECT_EQ(parallel_trace.status, serial_trace.status);
            EXPECT_EQ(parallel_trace.goal_state_index.has_value(), serial_trace.goal_state_index.has_value());
            EXPECT_EQ(parallel_trace.effective_width, serial_trace.effective_width);
            EXPECT_EQ(parallel_trace.num_generated_until_g_value_by_arity, serial_trace.num_generated_until_g_value_by_arity);
            EXPECT_EQ(parallel_trace.num_expanded_until_g_value_by_arity, serial_trace.num_expanded_until_g_value_by_arity);
            EXPECT_EQ(parallel_trace.num_pruned_until_g_value_by_arity, serial_trace.num_pruned_until_g_value_by_arity);
            EXPECT_EQ(parallel_trace.plan_action_signatures.size(), serial_trace.plan_action_signatures.size());
        }
    }
}

TEST(MimirTests, SearchAlgorithmsIWParallelBeamLongRunStressDeliveryTest)
{
    auto run = [](const fs::path& problem_file, BeamNoveltyMode beam_novelty_mode, uint32_t beam_width, uint32_t parallel_threads)
    {
        auto iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), problem_file, 2);
        auto iw_event_handler = iw::DefaultEventHandlerImpl::create(iw.get_problem());

        auto options = iw::Options();
        options.max_arity = 2;
        options.iw_event_handler = iw_event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(iw.get_problem());
        options.beam_width = beam_width;
        options.beam_novelty_mode = beam_novelty_mode;
        options.parallel_beam_num_threads = parallel_threads;

        const auto result = iw::find_solution(iw.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(iw.get_search_context(), result);
        }

        return make_iw_run_trace(result, iw_event_handler->get_statistics());
    };

    const auto survivors_reference = run(fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"), BeamNoveltyMode::SURVIVORS_ONLY, 4, 1);
    const auto all_tested_reference = run(fs::path(std::string(DATA_DIR) + "delivery/test_problem2.pddl"), BeamNoveltyMode::ALL_TESTED, 128, 1);

    for (int repetition = 0; repetition < 32; ++repetition)
    {
        SCOPED_TRACE(repetition);
        expect_iw_run_traces_match(
            run(fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"), BeamNoveltyMode::SURVIVORS_ONLY, 4, 4),
            survivors_reference);
        expect_iw_run_traces_match(
            run(fs::path(std::string(DATA_DIR) + "delivery/test_problem2.pddl"), BeamNoveltyMode::ALL_TESTED, 128, 4),
            all_tested_reference);
    }
}

TEST(MimirTests, SearchAlgorithmsIWRelaxedSurvivorsOnlyBeamRepeatedLiftedKPKCTest)
{
    auto run = [](const std::string& domain_name)
    {
        auto iw = LiftedIWPlanner(fs::path(std::string(DATA_DIR) + domain_name + "/domain.pddl"),
                                  fs::path(std::string(DATA_DIR) + domain_name + "/test_problem.pddl"),
                                  3);
        auto iw_event_handler = iw::DefaultEventHandlerImpl::create(iw.get_problem());

        auto options = iw::Options();
        options.max_arity = 3;
        options.iw_event_handler = iw_event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(iw.get_problem());
        options.beam_width = 64;
        options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
        options.parallel_beam_num_threads = 4;
        options.relaxed_survivors_only_beam = true;

        const auto result = iw::find_solution(iw.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(iw.get_search_context(), result);
        }

        return make_iw_run_trace(result, iw_event_handler->get_statistics());
    };

    for (const auto& domain_name : { std::string("delivery"), std::string("philosophers") })
    {
        SCOPED_TRACE(domain_name);
        const auto reference_trace = run(domain_name);

        for (int repetition = 0; repetition < 8; ++repetition)
        {
            SCOPED_TRACE(repetition);
            expect_iw_run_traces_match(run(domain_name), reference_trace);
        }
    }
}

TEST(MimirTests, SearchAlgorithmsIWParallelBeamMatchesSerialLiftedKPKCTest)
{
    struct TestCase
    {
        std::string domain_name;
        std::string problem_name;
        size_t max_arity;
        uint32_t beam_width;
    };

    auto run = [](const TestCase& test_case,
                  SearchContextImpl::SymmetryPruning symmetry_pruning,
                  BeamNoveltyMode beam_novelty_mode,
                  uint32_t parallel_threads)
    {
        auto iw = LiftedIWPlanner(fs::path(std::string(DATA_DIR) + test_case.domain_name + "/domain.pddl"),
                                  fs::path(std::string(DATA_DIR) + test_case.domain_name + "/" + test_case.problem_name),
                                  test_case.max_arity,
                                  symmetry_pruning);
        auto iw_event_handler = iw::DefaultEventHandlerImpl::create(iw.get_problem());

        auto options = iw::Options();
        options.max_arity = test_case.max_arity;
        options.iw_event_handler = iw_event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(iw.get_problem());
        options.beam_width = test_case.beam_width;
        options.beam_novelty_mode = beam_novelty_mode;
        options.parallel_beam_num_threads = parallel_threads;

        const auto result = iw::find_solution(iw.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(iw.get_search_context(), result);
        }

        return make_iw_run_trace(result, iw_event_handler->get_statistics());
    };

    for (const auto& test_case : std::vector<TestCase> { { "delivery", "test_problem.pddl", 3, 64 }, { "philosophers", "test_problem.pddl", 3, 64 } })
    {
        SCOPED_TRACE(test_case.domain_name);

        for (const auto beam_novelty_mode : { BeamNoveltyMode::ALL_TESTED, BeamNoveltyMode::SURVIVORS_ONLY })
        {
            SCOPED_TRACE(beam_novelty_mode == BeamNoveltyMode::ALL_TESTED ? "all_tested" : "survivors_only");
            const auto serial_trace = run(test_case, SearchContextImpl::SymmetryPruning::OFF, beam_novelty_mode, 1);

            for (const auto parallel_threads : { 2u, 4u })
            {
                SCOPED_TRACE(parallel_threads);
                expect_iw_run_traces_match(run(test_case, SearchContextImpl::SymmetryPruning::OFF, beam_novelty_mode, parallel_threads),
                                           serial_trace);
            }
        }
    }
}

TEST(MimirTests, SearchAlgorithmsIWParallelBeamMatchesSerialLiftedSymmetryPruningTest)
{
    auto run = [](BeamNoveltyMode beam_novelty_mode, uint32_t parallel_threads)
    {
        auto iw = LiftedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                  fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"),
                                  3,
                                  SearchContextImpl::SymmetryPruning::GI);
        auto iw_event_handler = iw::DefaultEventHandlerImpl::create(iw.get_problem());

        auto options = iw::Options();
        options.max_arity = 3;
        options.iw_event_handler = iw_event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(iw.get_problem());
        options.beam_width = 64;
        options.beam_novelty_mode = beam_novelty_mode;
        options.parallel_beam_num_threads = parallel_threads;

        const auto result = iw::find_solution(iw.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(iw.get_search_context(), result);
        }

        return make_iw_run_trace(result, iw_event_handler->get_statistics());
    };

    for (const auto beam_novelty_mode : { BeamNoveltyMode::ALL_TESTED, BeamNoveltyMode::SURVIVORS_ONLY })
    {
        SCOPED_TRACE(beam_novelty_mode == BeamNoveltyMode::ALL_TESTED ? "all_tested" : "survivors_only");
        const auto serial_trace = run(beam_novelty_mode, 1);

        for (const auto parallel_threads : { 2u, 4u })
        {
            SCOPED_TRACE(parallel_threads);
            expect_iw_run_traces_match(run(beam_novelty_mode, parallel_threads), serial_trace);
        }
    }
}

TEST(MimirTests, SearchAlgorithmsIWParallelBeamRepeatedMatchesSerialLiftedKPKCTest)
{
    struct TestCase
    {
        std::string domain_name;
        BeamNoveltyMode beam_novelty_mode;
    };

    auto run = [](const std::string& domain_name, BeamNoveltyMode beam_novelty_mode, uint32_t parallel_threads)
    {
        auto iw = LiftedIWPlanner(fs::path(std::string(DATA_DIR) + domain_name + "/domain.pddl"),
                                  fs::path(std::string(DATA_DIR) + domain_name + "/test_problem.pddl"),
                                  3);
        auto iw_event_handler = iw::DefaultEventHandlerImpl::create(iw.get_problem());

        auto options = iw::Options();
        options.max_arity = 3;
        options.iw_event_handler = iw_event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(iw.get_problem());
        options.beam_width = 64;
        options.beam_novelty_mode = beam_novelty_mode;
        options.parallel_beam_num_threads = parallel_threads;

        const auto result = iw::find_solution(iw.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(iw.get_search_context(), result);
        }

        return make_iw_run_trace(result, iw_event_handler->get_statistics());
    };

    for (const auto& test_case :
         std::vector<TestCase> { { "delivery", BeamNoveltyMode::ALL_TESTED },
                                 { "delivery", BeamNoveltyMode::SURVIVORS_ONLY },
                                 { "philosophers", BeamNoveltyMode::ALL_TESTED },
                                 { "philosophers", BeamNoveltyMode::SURVIVORS_ONLY } })
    {
        SCOPED_TRACE(test_case.domain_name);
        SCOPED_TRACE(test_case.beam_novelty_mode == BeamNoveltyMode::ALL_TESTED ? "all_tested" : "survivors_only");

        const auto serial_trace = run(test_case.domain_name, test_case.beam_novelty_mode, 1);
        for (int repetition = 0; repetition < 20; ++repetition)
        {
            SCOPED_TRACE(repetition);
            for (const auto parallel_threads : { 2u, 4u })
            {
                SCOPED_TRACE(parallel_threads);
                expect_iw_run_traces_match(run(test_case.domain_name, test_case.beam_novelty_mode, parallel_threads), serial_trace);
            }
        }
    }
}

TEST(MimirTests, SearchAlgorithmsIWParallelBeamTieBreakingMatchesSerialLiftedKPKCTest)
{
    auto run = [](bool randomize_equal_score_ties, uint64_t seed, uint32_t parallel_threads)
    {
        auto iw = LiftedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                  fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"),
                                  3);
        auto iw_event_handler = iw::DefaultEventHandlerImpl::create(iw.get_problem());

        auto options = iw::Options();
        options.max_arity = 3;
        options.iw_event_handler = iw_event_handler;
        options.layer_ordering_strategy = std::make_shared<ConstantScoringLayerOrderingStrategy>();
        options.beam_width = 64;
        options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
        options.randomize_equal_score_ties = randomize_equal_score_ties;
        options.equal_score_tie_seed = seed;
        options.parallel_beam_num_threads = parallel_threads;

        const auto result = iw::find_solution(iw.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(iw.get_search_context(), result);
        }

        return make_iw_run_trace(result, iw_event_handler->get_statistics());
    };

    const auto serial_default = run(false, 0, 1);
    const auto serial_seeded = run(true, 7, 1);

    for (const auto parallel_threads : { 2u, 4u })
    {
        expect_iw_run_traces_match(run(false, 0, parallel_threads), serial_default);
        expect_iw_run_traces_match(run(true, 7, parallel_threads), serial_seeded);
    }
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
