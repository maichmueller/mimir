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
#include "mimir/search/algorithms/strategies/transition_ordering_strategy.hpp"
#include "mimir/search/landmarks/fact_landmark_generator.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"
#include "mimir/formalism/action.hpp"
#include "mimir/formalism/ground_action.hpp"
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
#include <cstdint>
#include <ostream>
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

    throw std::runtime_error("SearchAlgorithmsIWAbstractedArityOneNoveltyPruningStrategyTest: Could not find suitable projected-atom test candidate.");
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

TEST(MimirTests, SearchAlgorithmsIWArityOneNoveltyPruningStrategyRootDepthOneContinuationTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "gripper/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl");
    const auto problem = ProblemImpl::create(domain_file, problem_file);

    const auto search_context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));
    auto& state_repository = *search_context->get_state_repository();

    auto fluent_atoms = GroundAtomList<FluentTag> {};
    for (const auto& atom : problem->get_repositories().get_ground_atoms<FluentTag>())
    {
        fluent_atoms.push_back(&atom);
    }
    ASSERT_FALSE(fluent_atoms.empty());

    const auto numeric_values = problem->get_initial_function_to_value<FluentTag>();
    const auto [state, state_metric_value] = state_repository.get_or_create_state(GroundAtomList<FluentTag> { fluent_atoms[0] }, numeric_values);
    const auto [succ_state, succ_state_metric_value] = state_repository.get_or_create_state(GroundAtomList<FluentTag> {}, numeric_values);
    [[maybe_unused]] const auto ignored_state_metric_value = state_metric_value;
    [[maybe_unused]] const auto ignored_succ_state_metric_value = succ_state_metric_value;

    const auto iw1 = iw::ArityKNoveltyPruningStrategyImpl::create(1, fluent_atoms.size(), true);

    EXPECT_FALSE(iw1->test_prune_initial_state(state));
    EXPECT_TRUE(iw1->should_bypass_action_add_effect_precheck(state));
    EXPECT_FALSE(iw1->test_prune_successor_state(state, succ_state, true));
    EXPECT_TRUE(iw1->consume_skip_state_expansion(succ_state));
    EXPECT_FALSE(iw1->consume_skip_state_expansion(succ_state));
}

TEST(MimirTests, SearchAlgorithmsIWAbstractedArityOneUntypedProjectiveParityTest)
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
    std::ranges::sort(covering_atoms, [](const auto lhs, const auto rhs) { return lhs->get_index() < rhs->get_index(); });
    covering_atoms.erase(std::unique(covering_atoms.begin(), covering_atoms.end()), covering_atoms.end());
    const auto [succ_state, succ_state_metric_value] = state_repository.get_or_create_state(covering_atoms, numeric_values);
    [[maybe_unused]] const auto ignored_succ_state_metric_value = succ_state_metric_value;

    const auto projective_iw1 = iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, true, false, false);
    const auto abstracted_iw1 = iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, true, false, false);

    EXPECT_EQ(projective_iw1->test_prune_initial_state(state), abstracted_iw1->test_prune_initial_state(state));
    EXPECT_EQ(projective_iw1->test_prune_successor_state(state, succ_state, true), abstracted_iw1->test_prune_successor_state(state, succ_state, true));
}

TEST(MimirTests, SearchAlgorithmsIWAbstractedArityOneTypedProjectiveParityTest)
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
    std::ranges::sort(covering_atoms, [](const auto lhs, const auto rhs) { return lhs->get_index() < rhs->get_index(); });
    covering_atoms.erase(std::unique(covering_atoms.begin(), covering_atoms.end()), covering_atoms.end());
    const auto [succ_state, succ_state_metric_value] = state_repository.get_or_create_state(covering_atoms, numeric_values);
    [[maybe_unused]] const auto ignored_succ_state_metric_value = succ_state_metric_value;

    const auto projective_iw1 = iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, false, false, false);
    const auto abstracted_iw1 = iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, false, false, false);

    EXPECT_EQ(projective_iw1->test_prune_initial_state(state), abstracted_iw1->test_prune_initial_state(state));
    EXPECT_EQ(projective_iw1->test_prune_successor_state(state, succ_state, true), abstracted_iw1->test_prune_successor_state(state, succ_state, true));
}


namespace
{
struct AbstractedProjectiveParityRun
{
    SearchStatus status;
    bool has_plan;
    bool has_goal_state;
    size_t generated;
    size_t novel_generated;
    std::vector<std::string> action_signatures;
};

struct IncrementalStatisticsSignature
{
    uint64_t root_actions_fully_enumerated;
    uint64_t non_root_states_using_incremental_path;
    uint64_t changed_atoms_processed;
    uint64_t trigger_records_visited;
    uint64_t partial_seeds_created;
    uint64_t ground_actions_returned_by_partial_completion;
    uint64_t local_duplicate_candidates_removed;
    uint64_t already_tested_actions_skipped;
    uint64_t non_root_states_with_zero_returned_actions;

    bool operator==(const IncrementalStatisticsSignature&) const = default;
};

std::ostream& operator<<(std::ostream& out, const IncrementalStatisticsSignature& signature)
{
    return out << "{root=" << signature.root_actions_fully_enumerated << ", non_root=" << signature.non_root_states_using_incremental_path
               << ", changed=" << signature.changed_atoms_processed << ", triggers=" << signature.trigger_records_visited
               << ", seeds=" << signature.partial_seeds_created << ", returned=" << signature.ground_actions_returned_by_partial_completion
               << ", duplicates=" << signature.local_duplicate_candidates_removed << ", skipped=" << signature.already_tested_actions_skipped
               << ", zero=" << signature.non_root_states_with_zero_returned_actions << "}";
}

IncrementalStatisticsSignature make_incremental_statistics_signature(const brfs::IW1IncrementalFirstApplicabilityStatistics& statistics)
{
    return IncrementalStatisticsSignature { statistics.get_num_root_actions_fully_enumerated(),
                                            statistics.get_num_non_root_states_using_incremental_path(),
                                            statistics.get_num_changed_atoms_processed(),
                                            statistics.get_num_trigger_records_visited(),
                                            statistics.get_num_partial_seeds_created(),
                                            statistics.get_num_ground_actions_returned_by_partial_completion(),
                                            statistics.get_num_local_duplicate_candidates_removed(),
                                            statistics.get_num_already_tested_actions_skipped(),
                                            statistics.get_num_non_root_states_with_zero_returned_actions() };
}

struct AbstractedProjectiveIncrementalParityRun
{
    SearchStatus status;
    bool has_plan;
    bool has_goal_state;
    size_t generated;
    size_t novel_generated;
    std::vector<std::string> action_signatures;
    IncrementalStatisticsSignature incremental_statistics;
};

std::vector<std::string> plan_action_signatures(const Plan& plan)
{
    auto signatures = std::vector<std::string> {};
    for (const auto& action : plan.get_actions())
    {
        auto signature = action->get_action()->get_name();
        signature += "(";
        const auto& objects = action->get_objects();
        for (size_t i = 0; i < objects.size(); ++i)
        {
            if (i > 0)
            {
                signature += ",";
            }
            signature += objects[i]->get_name();
        }
        signature += ")";
        signatures.push_back(signature);
    }
    return signatures;
}

AbstractedProjectiveParityRun run_projective_or_abstracted_iw1(const fs::path& domain_file,
                                                                const fs::path& problem_file,
                                                                bool typed,
                                                                bool abstracted,
                                                                bool precheck_add_effect_novelty,
                                                                bool atom_first_mode)
{
    const auto search_context = SearchContextImpl::create(domain_file, problem_file, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
    const auto problem = search_context->get_problem();
    auto event_handler = brfs::DefaultEventHandlerImpl::create(problem, true);

    auto options = brfs::Options();
    options.event_handler = event_handler;
    options.pruning_strategy = abstracted
        ? iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, !typed, false, false)
        : iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, !typed, false, false);
    options.iw1_precheck_add_effect_novelty = precheck_add_effect_novelty;
    options.iw1_atom_first_mode = atom_first_mode;

    const auto result = brfs::find_solution(search_context, options);
    const auto& statistics = event_handler->get_statistics();
    return AbstractedProjectiveParityRun { result.status,
                                           result.plan.has_value(),
                                           result.goal_state.has_value(),
                                           statistics.get_num_generated(),
                                           statistics.get_num_generated_in_search_tree(),
                                           result.plan.has_value() ? plan_action_signatures(result.plan.value()) : std::vector<std::string> {} };
}

void expect_projective_abstracted_iw1_search_parity(const fs::path& domain_file, const fs::path& problem_file, bool typed)
{
    for (const auto& mode : std::array<std::pair<bool, bool>, 3> { std::pair { false, false }, std::pair { true, false }, std::pair { true, true } })
    {
        const auto precheck_add_effect_novelty = mode.first;
        const auto atom_first_mode = mode.second;
        const auto projective = run_projective_or_abstracted_iw1(
            domain_file,
            problem_file,
            typed,
            false,
            precheck_add_effect_novelty,
            atom_first_mode);
        const auto abstracted = run_projective_or_abstracted_iw1(
            domain_file,
            problem_file,
            typed,
            true,
            precheck_add_effect_novelty,
            atom_first_mode);

        EXPECT_EQ(abstracted.status, projective.status);
        EXPECT_EQ(abstracted.has_plan, projective.has_plan);
        EXPECT_EQ(abstracted.has_goal_state, projective.has_goal_state);
        EXPECT_EQ(abstracted.generated, projective.generated);
        EXPECT_EQ(abstracted.novel_generated, projective.novel_generated);
        EXPECT_EQ(abstracted.action_signatures, projective.action_signatures);
    }
}

AbstractedProjectiveIncrementalParityRun run_projective_or_abstracted_iw1_incremental(bool typed, bool abstracted, bool beam_all_tested)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "iw1_incremental/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "iw1_incremental/positive_problem.pddl");
    const auto search_context = SearchContextImpl::create(domain_file, problem_file, SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));
    const auto problem = search_context->get_problem();
    auto event_handler = brfs::DefaultEventHandlerImpl::create(problem, true);

    auto options = brfs::Options();
    options.event_handler = event_handler;
    options.pruning_strategy = abstracted
        ? iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, !typed, false, false)
        : iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, !typed, false, false);
    options.iw1_precheck_add_effect_novelty = true;
    options.iw1_incremental_first_applicability = true;
    options.iw1_incremental_first_applicability_debug_crosscheck = true;
    if (beam_all_tested)
    {
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
        options.beam_width = 4;
        options.beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
    }

    const auto result = brfs::find_solution(search_context, options);
    const auto& statistics = event_handler->get_statistics();
    return AbstractedProjectiveIncrementalParityRun { result.status,
                                                      result.plan.has_value(),
                                                      result.goal_state.has_value(),
                                                      statistics.get_num_generated(),
                                                      statistics.get_num_generated_in_search_tree(),
                                                      result.plan.has_value() ? plan_action_signatures(result.plan.value()) : std::vector<std::string> {},
                                                      make_incremental_statistics_signature(
                                                          statistics.get_iw1_incremental_first_applicability_statistics()) };
}

void expect_projective_abstracted_iw1_incremental_parity(bool typed, bool beam_all_tested)
{
    const auto projective = run_projective_or_abstracted_iw1_incremental(typed, false, beam_all_tested);
    const auto abstracted = run_projective_or_abstracted_iw1_incremental(typed, true, beam_all_tested);

    EXPECT_EQ(abstracted.status, projective.status);
    EXPECT_EQ(abstracted.has_plan, projective.has_plan);
    EXPECT_EQ(abstracted.has_goal_state, projective.has_goal_state);
    EXPECT_EQ(abstracted.generated, projective.generated);
    EXPECT_EQ(abstracted.novel_generated, projective.novel_generated);
    EXPECT_EQ(abstracted.action_signatures, projective.action_signatures);
    EXPECT_EQ(abstracted.incremental_statistics, projective.incremental_statistics);
    EXPECT_GE(abstracted.incremental_statistics.non_root_states_using_incremental_path, 1);
    if (beam_all_tested)
    {
        EXPECT_GE(abstracted.incremental_statistics.root_actions_fully_enumerated, 1);
    }
}
}  // namespace

TEST(MimirTests, SearchAlgorithmsIWAbstractedArityOneUntypedProjectiveFullSearchParityTest)
{
    expect_projective_abstracted_iw1_search_parity(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"),
                                                   fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"),
                                                   false);
}

TEST(MimirTests, SearchAlgorithmsIWAbstractedArityOneTypedProjectiveFullSearchParityTest)
{
    expect_projective_abstracted_iw1_search_parity(fs::path(std::string(DATA_DIR) + "driverlog/domain.pddl"),
                                                   fs::path(std::string(DATA_DIR) + "driverlog/test_problem.pddl"),
                                                   true);
}

TEST(MimirTests, SearchAlgorithmsIWAbstractedArityOneUntypedProjectiveIncrementalPlainParityTest)
{
    expect_projective_abstracted_iw1_incremental_parity(false, false);
}

TEST(MimirTests, SearchAlgorithmsIWAbstractedArityOneTypedProjectiveIncrementalPlainParityTest)
{
    expect_projective_abstracted_iw1_incremental_parity(true, false);
}

TEST(MimirTests, SearchAlgorithmsIWAbstractedArityOneUntypedProjectiveIncrementalBeamAllTestedParityTest)
{
    expect_projective_abstracted_iw1_incremental_parity(false, true);
}

TEST(MimirTests, SearchAlgorithmsIWAbstractedArityOneTypedProjectiveIncrementalBeamAllTestedParityTest)
{
    expect_projective_abstracted_iw1_incremental_parity(true, true);
}

TEST(MimirTests, SearchAlgorithmsIWAbstractedGoalAtomsAreNotAbstractedTest)
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
    std::ranges::sort(covering_atoms, [](const auto lhs, const auto rhs) { return lhs->get_index() < rhs->get_index(); });
    covering_atoms.erase(std::unique(covering_atoms.begin(), covering_atoms.end()), covering_atoms.end());
    const auto [succ_state, succ_state_metric_value] = state_repository.get_or_create_state(covering_atoms, numeric_values);
    [[maybe_unused]] const auto ignored_succ_state_metric_value = succ_state_metric_value;

    const auto abstracted_iw1 = iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, true, false, false);
    const auto goal_preserving_aiw1 = iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, true, true, false);

    EXPECT_FALSE(abstracted_iw1->test_prune_initial_state(state));
    EXPECT_FALSE(goal_preserving_aiw1->test_prune_initial_state(state));
    [[maybe_unused]] const auto ignored_abstracted_prune = abstracted_iw1->test_prune_successor_state(state, succ_state, true);
    EXPECT_FALSE(goal_preserving_aiw1->test_prune_successor_state(state, succ_state, true));
}

TEST(MimirTests, SearchAlgorithmsIWPreservedGoalAtomsRetainAbstractedFeaturesTest)
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
    const auto covering_atom = problem->get_or_create_ground_atom<FluentTag>(at_predicate, ObjectList { truck1, s1 });
    const auto target_goal_atom = problem->get_or_create_ground_atom<FluentTag>(at_predicate, ObjectList { package1, s0 });
    const auto projected_follower_atom = problem->get_or_create_ground_atom<FluentTag>(at_predicate, ObjectList { package1, s1 });
    const auto numeric_values = problem->get_initial_function_to_value<FluentTag>();

    const auto [state, state_metric_value] = state_repository.get_or_create_state(GroundAtomList<FluentTag> { covering_atom }, numeric_values);
    [[maybe_unused]] const auto ignored_state_metric_value = state_metric_value;
    const auto [goal_state, goal_state_metric_value] =
        state_repository.get_or_create_state(GroundAtomList<FluentTag> { covering_atom, target_goal_atom }, numeric_values);
    [[maybe_unused]] const auto ignored_goal_state_metric_value = goal_state_metric_value;
    const auto [following_state, following_state_metric_value] = state_repository.get_or_create_state(
        GroundAtomList<FluentTag> { covering_atom, target_goal_atom, projected_follower_atom },
        numeric_values);
    [[maybe_unused]] const auto ignored_following_state_metric_value = following_state_metric_value;

    const auto goal_preserving_aiw1 = iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, true, true, false);
    EXPECT_FALSE(goal_preserving_aiw1->test_prune_initial_state(state));
    EXPECT_FALSE(goal_preserving_aiw1->test_prune_successor_state(state, goal_state, true));
    EXPECT_TRUE(goal_preserving_aiw1->test_prune_successor_state(goal_state, following_state, true));
}

TEST(MimirTests, SearchAlgorithmsIWAbstractedLazyAtomsWidthTwoAndThreeRegressionTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "driverlog/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "driverlog/test_problem.pddl");

    const auto run_regression = [&](size_t width)
    {
        const auto problem = ProblemImpl::create(domain_file, problem_file);
        const auto search_context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));
        auto& state_repository = *search_context->get_state_repository();
        const auto [state, state_metric_value] = state_repository.get_or_create_initial_state();
        [[maybe_unused]] const auto ignored_state_metric_value = state_metric_value;

        const auto abstracted_iw = iw::AbstractedNoveltyPruningStrategyImpl::create(problem, width, true, false, false);

        const auto at_predicate = problem->get_domain()->get_predicate<FluentTag>("at");
        const auto driver1 = problem->get_problem_or_domain_object("driver1");
        const auto truck1 = problem->get_problem_or_domain_object("truck1");
        const auto s1 = problem->get_problem_or_domain_object("s1");
        const auto p0_1 = problem->get_problem_or_domain_object("p0-1");

        auto successor_atoms = problem->get_fluent_initial_atoms();
        successor_atoms.push_back(problem->get_or_create_ground_atom<FluentTag>(at_predicate, ObjectList { driver1, s1 }));
        successor_atoms.push_back(problem->get_or_create_ground_atom<FluentTag>(at_predicate, ObjectList { truck1, p0_1 }));
        std::ranges::sort(successor_atoms, [](const auto lhs, const auto rhs) { return lhs->get_index() < rhs->get_index(); });
        successor_atoms.erase(std::unique(successor_atoms.begin(), successor_atoms.end()), successor_atoms.end());

        const auto [succ_state, succ_state_metric_value] =
            state_repository.get_or_create_state(successor_atoms, problem->get_initial_function_to_value<FluentTag>());
        [[maybe_unused]] const auto ignored_succ_state_metric_value = succ_state_metric_value;

        EXPECT_FALSE(abstracted_iw->test_prune_initial_state(state));
        EXPECT_NO_THROW((void) abstracted_iw->test_prune_successor_state(state, succ_state, true));
    };

    for (const auto width : std::array<size_t, 2> { 2, 3 })
    {
        run_regression(width);
    }
}

TEST(MimirTests, SearchAlgorithmsIWAbstractedWidthTwoAndThreeSmokeTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "driverlog/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "driverlog/test_problem.pddl");
    const auto problem = ProblemImpl::create(domain_file, problem_file);
    const auto search_context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));
    auto& state_repository = *search_context->get_state_repository();
    const auto [state, state_metric_value] = state_repository.get_or_create_initial_state();
    [[maybe_unused]] const auto ignored_state_metric_value = state_metric_value;

    for (const auto width : std::array<size_t, 2> { 2, 3 })
    {
        auto options = brfs::Options();
        options.start_state = state;
        options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(problem, width, false, true, true);
        options.max_depth = 1;
        EXPECT_NO_THROW(brfs::find_solution(search_context, options));
    }
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

TEST(MimirTests, SearchAlgorithmsIWArityOneIncrementalAddEffectPrecheckPlainTest)
{
    auto iw = LiftedIWPlanner(fs::path(std::string(DATA_DIR) + "iw1_incremental/domain.pddl"),
                              fs::path(std::string(DATA_DIR) + "iw1_incremental/positive_problem.pddl"),
                              1);
    auto iw_event_handler = iw::DefaultEventHandlerImpl::create(iw.get_problem());
    auto brfs_event_handler = brfs::DefaultEventHandlerImpl::create(iw.get_problem());

    auto options = iw::Options();
    options.max_arity = 1;
    options.iw_event_handler = iw_event_handler;
    options.brfs_event_handler = brfs_event_handler;
    options.iw1_precheck_add_effect_novelty = true;
    options.iw1_incremental_first_applicability = true;
    options.iw1_incremental_first_applicability_debug_crosscheck = true;

    const auto result = iw::find_solution(iw.get_search_context(), options);

    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(result.plan.has_value());
    EXPECT_EQ(get_plan_action_signatures(*result.plan), std::vector<std::string>({ "enable(a)", "use-enabled(a)" }));

    const auto& brfs_statistics_by_arity = iw_event_handler->get_statistics().get_brfs_statistics_by_arity();
    ASSERT_EQ(brfs_statistics_by_arity.size(), 2);
    const auto& iw1_incremental_statistics = brfs_statistics_by_arity[1].get_iw1_incremental_first_applicability_statistics();
    EXPECT_GE(iw1_incremental_statistics.get_num_non_root_states_using_incremental_path(), 1);
}

TEST(MimirTests, SearchAlgorithmsIWArityOneIncrementalAddEffectPrecheckBeamAllTestedTest)
{
    auto iw = LiftedIWPlanner(fs::path(std::string(DATA_DIR) + "iw1_incremental/domain.pddl"),
                              fs::path(std::string(DATA_DIR) + "iw1_incremental/positive_problem.pddl"),
                              1);
    auto iw_event_handler = iw::DefaultEventHandlerImpl::create(iw.get_problem());
    auto brfs_event_handler = brfs::DefaultEventHandlerImpl::create(iw.get_problem());

    auto options = iw::Options();
    options.max_arity = 1;
    options.iw_event_handler = iw_event_handler;
    options.brfs_event_handler = brfs_event_handler;
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(iw.get_problem());
    options.beam_width = 4;
    options.beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
    options.iw1_precheck_add_effect_novelty = true;
    options.iw1_incremental_first_applicability = true;
    options.iw1_incremental_first_applicability_debug_crosscheck = true;

    const auto result = iw::find_solution(iw.get_search_context(), options);

    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(result.plan.has_value());
    EXPECT_EQ(get_plan_action_signatures(*result.plan), std::vector<std::string>({ "enable(a)", "use-enabled(a)" }));

    const auto& brfs_statistics_by_arity = iw_event_handler->get_statistics().get_brfs_statistics_by_arity();
    ASSERT_EQ(brfs_statistics_by_arity.size(), 2);
    const auto& iw1_incremental_statistics = brfs_statistics_by_arity[1].get_iw1_incremental_first_applicability_statistics();
    EXPECT_GE(iw1_incremental_statistics.get_num_root_actions_fully_enumerated(), 1);
    EXPECT_GE(iw1_incremental_statistics.get_num_non_root_states_using_incremental_path(), 1);
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

TEST(MimirTests, SearchAlgorithmsIWMaxDepthMatchesSolutionBoundaryTest)
{
    auto shallow_iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"), 2);
    auto shallow_options = iw::Options();
    shallow_options.max_arity = 2;
    shallow_options.max_depth = 3;

    const auto shallow_result = iw::find_solution(shallow_iw.get_search_context(), shallow_options);
    EXPECT_EQ(shallow_result.status, SearchStatus::FAILED);
    EXPECT_FALSE(shallow_result.plan.has_value());

    auto exact_iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"), 2);
    auto exact_options = iw::Options();
    exact_options.max_arity = 2;
    exact_options.max_depth = 4;

    const auto exact_result = iw::find_solution(exact_iw.get_search_context(), exact_options);
    ASSERT_EQ(exact_result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(exact_result.plan.has_value());
    EXPECT_EQ(exact_result.plan->get_actions().size(), 4);
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


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Landmark-informed transition ordering
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static landmarks::FactLandmarkGraph make_iw_landmarks(const Problem& problem,
                                                       landmarks::FactLandmarkGeneratorOptions options = landmarks::FactLandmarkGeneratorOptions())
{
    const auto grounder = LiftedGrounder(problem);
    return landmarks::ApproximateFactLandmarkGenerator::create(grounder, options);
}

static std::vector<std::string> get_iw_plan_action_names(const Plan& plan)
{
    auto names = std::vector<std::string> {};
    names.reserve(plan.get_actions().size());

    for (const auto action : plan.get_actions())
    {
        names.push_back(action->get_action()->get_name());
    }

    return names;
}

/// Acceptance criterion #8 for iw.cpp: the 2-argument `iw::find_solution(context, options)` overload
/// -- now routed through `find_solution_impl<QueuedTransitionOrderingStrategy>` -- must reproduce the
/// pre-change status/plan/statistics. Numbers are those asserted by the pre-existing delivery and
/// miconic-fulladl IW tests in this file.
TEST(MimirTests, SearchAlgorithmsIWDefaultOverloadBehaviorParityTest)
{
    {
        auto grounded_iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                             fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"),
                                             3);
        const auto grounded_result = grounded_iw.find_solution();
        EXPECT_EQ(grounded_result.status, SearchStatus::SOLVED);
        ASSERT_TRUE(grounded_result.plan.has_value());
        EXPECT_EQ(grounded_result.plan->get_actions().size(), 4u);

        const auto& statistics = grounded_iw.get_iw_statistics();
        EXPECT_EQ(statistics.get_brfs_statistics_by_arity().back().get_num_generated_until_g_value().back(), 18u);
        EXPECT_EQ(statistics.get_brfs_statistics_by_arity().back().get_num_expanded_until_g_value().back(), 7u);
        EXPECT_EQ(statistics.get_effective_width(), 2u);

        auto lifted_iw = LiftedIWPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                         fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"),
                                         3);
        const auto lifted_result = lifted_iw.find_solution();
        EXPECT_EQ(lifted_result.status, SearchStatus::SOLVED);
        ASSERT_TRUE(lifted_result.plan.has_value());
        EXPECT_EQ(lifted_result.plan->get_actions().size(), 4u);
        EXPECT_EQ(lifted_iw.get_iw_statistics().get_effective_width(), 2u);
        EXPECT_EQ(get_iw_plan_action_names(*grounded_result.plan), get_iw_plan_action_names(*lifted_result.plan));
    }

    {
        auto grounded_iw = GroundedIWPlanner(fs::path(std::string(DATA_DIR) + "miconic-fulladl/domain.pddl"),
                                             fs::path(std::string(DATA_DIR) + "miconic-fulladl/test_problem.pddl"),
                                             3);
        const auto grounded_result = grounded_iw.find_solution();
        EXPECT_EQ(grounded_result.status, SearchStatus::SOLVED);
        ASSERT_TRUE(grounded_result.plan.has_value());
        EXPECT_EQ(grounded_result.plan->get_actions().size(), 7u);

        const auto& statistics = grounded_iw.get_iw_statistics();
        EXPECT_EQ(statistics.get_brfs_statistics_by_arity().back().get_num_generated_until_g_value().back(), 69u);
        EXPECT_EQ(statistics.get_brfs_statistics_by_arity().back().get_num_expanded_until_g_value().back(), 27u);
        EXPECT_EQ(statistics.get_effective_width(), 2u);
    }
}

/// Composition with `ArityKNoveltyPruningStrategyImpl`'s `optimize_root_depth_one_continuation`
/// (always active for `iw::Options::max_arity == 1`): depth-1 successors are never pruned, but
/// non-novel ones are flagged for skip-expansion via `consume_skip_state_expansion`. Since the
/// deferred admission pass calls `test_prune_successor_state` in SORTED order, the landmark-preferred
/// depth-1 transition claims the shared novelty witness first and keeps its expansion rights, while
/// the lower-scored duplicate is the one that gets skip-flagged.
///
/// Both depth-1 transitions of this fixture add exactly the same single fluent atom (w), so exactly
/// one of them can be novel. Default IW(1) admits the first-enumerated `del-landmark` (which destroys
/// the landmark (p)) and skip-flags the other, so `finish` is never expanded and IW(1) fails. The
/// landmark ordering reverses which one is skip-flagged and solves at width 1.
TEST(MimirTests, SearchAlgorithmsIWLandmarkOrderingKeepsRootDepthOneExpansionRightsTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "landmark_transition_ordering/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "landmark_transition_ordering/test_problem.pddl");

    const auto baseline_problem = ProblemImpl::create(domain_file, problem_file);
    const auto baseline_context = SearchContextImpl::create(baseline_problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));

    auto baseline_options = iw::Options();
    baseline_options.max_arity = 1;

    const auto baseline_result = iw::find_solution(baseline_context, baseline_options);
    EXPECT_EQ(baseline_result.status, SearchStatus::FAILED);
    EXPECT_FALSE(baseline_result.plan.has_value());

    const auto landmark_problem = ProblemImpl::create(domain_file, problem_file);
    const auto landmark_graph = make_iw_landmarks(landmark_problem);
    const auto landmark_context = SearchContextImpl::create(landmark_problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));

    auto landmark_options = iw::Options();
    landmark_options.max_arity = 1;

    const auto landmark_result = iw::find_solution(landmark_context, landmark_options, LandmarkTransitionOrderingStrategy(landmark_graph));
    ASSERT_EQ(landmark_result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(landmark_result.plan.has_value());
    EXPECT_EQ(get_iw_plan_action_names(*landmark_result.plan), (std::vector<std::string> { "del-nonlandmark", "finish" }));
}

/// With an empty landmark set every transition scores identically, so `std::stable_sort` must keep raw
/// generation order and the deferred width-1 pass must behave exactly like the default queued one.
TEST(MimirTests, SearchAlgorithmsIWLandmarkOrderingWithEqualScoresMatchesDefaultTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "delivery/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl");

    const auto baseline_problem = ProblemImpl::create(domain_file, problem_file);
    const auto baseline_context = SearchContextImpl::create(baseline_problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
    const auto baseline_iw_event_handler = iw::DefaultEventHandlerImpl::create(baseline_problem);

    auto baseline_options = iw::Options();
    baseline_options.max_arity = 3;
    baseline_options.iw_event_handler = baseline_iw_event_handler;

    const auto baseline_result = iw::find_solution(baseline_context, baseline_options);
    ASSERT_EQ(baseline_result.status, SearchStatus::SOLVED);

    const auto ordered_problem = ProblemImpl::create(domain_file, problem_file);
    auto generator_options = landmarks::FactLandmarkGeneratorOptions();
    generator_options.include_positive_goal_facts = false;
    const auto empty_landmark_graph = make_iw_landmarks(ordered_problem, generator_options);
    ASSERT_TRUE(empty_landmark_graph->get_landmark_atom_indices().empty());

    const auto ordered_context = SearchContextImpl::create(ordered_problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
    const auto ordered_iw_event_handler = iw::DefaultEventHandlerImpl::create(ordered_problem);

    auto ordered_options = iw::Options();
    ordered_options.max_arity = 3;
    ordered_options.iw_event_handler = ordered_iw_event_handler;

    const auto ordered_result = iw::find_solution(ordered_context, ordered_options, LandmarkTransitionOrderingStrategy(empty_landmark_graph));

    ASSERT_EQ(ordered_result.status, baseline_result.status);
    ASSERT_TRUE(ordered_result.plan.has_value());
    EXPECT_EQ(get_iw_plan_action_names(*ordered_result.plan), get_iw_plan_action_names(*baseline_result.plan));
    EXPECT_EQ(ordered_iw_event_handler->get_statistics().get_effective_width(), baseline_iw_event_handler->get_statistics().get_effective_width());
    EXPECT_EQ(ordered_iw_event_handler->get_statistics().get_brfs_statistics_by_arity().back().get_num_generated_until_g_value(),
              baseline_iw_event_handler->get_statistics().get_brfs_statistics_by_arity().back().get_num_generated_until_g_value());
    EXPECT_EQ(ordered_iw_event_handler->get_statistics().get_brfs_statistics_by_arity().back().get_num_expanded_until_g_value(),
              baseline_iw_event_handler->get_statistics().get_brfs_statistics_by_arity().back().get_num_expanded_until_g_value());
}

/// `iw::Options` fields unsupported by the deferred admission loop must propagate the descriptive
/// `std::invalid_argument` raised by brfs.cpp rather than being silently ignored.
TEST(MimirTests, SearchAlgorithmsIWLandmarkOrderingRejectsUnsupportedOptionsTest)
{
    const auto problem = ProblemImpl::create(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"),
                                             fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    const auto landmark_graph = make_iw_landmarks(problem);
    const auto context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
    const auto ordering = LandmarkTransitionOrderingStrategy(landmark_graph);

    {
        auto options = iw::Options();
        options.max_arity = 1;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
        options.beam_width = 4;
        EXPECT_THROW(iw::find_solution(context, options, ordering), std::invalid_argument);
    }

    {
        auto options = iw::Options();
        options.max_arity = 1;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
        options.max_next_layer_states = 4;
        EXPECT_THROW(iw::find_solution(context, options, ordering), std::invalid_argument);
    }

    {
        auto options = iw::Options();
        options.max_arity = 1;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
        EXPECT_THROW(iw::find_solution(context, options, ordering), std::invalid_argument);
    }

    {
        auto options = iw::Options();
        options.max_arity = 1;
        options.iw1_atom_first_mode = true;
        EXPECT_THROW(iw::find_solution(context, options, ordering), std::invalid_argument);
    }

    {
        auto options = iw::Options();
        options.max_arity = 1;
        options.iw1_precheck_add_effect_novelty = true;
        EXPECT_THROW(iw::find_solution(context, options, ordering), std::invalid_argument);
    }
}


/// `iw::Options` used to accept `max_time_in_ms` and `max_num_states` nowhere and silently drop
/// them: the per-arity `brfs::Options` were built without either, so an IW search could not be
/// budgeted at all. These are the regression tests for forwarding them.
TEST(MimirTests, SearchAlgorithmsIWForwardsBudgetsTest)
{
    const auto make_context = [](const std::string& domain, const std::string& problem)
    {
        return SearchContextImpl::create(fs::path(std::string(DATA_DIR) + domain),
                                         fs::path(std::string(DATA_DIR) + problem),
                                         SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
    };

    {
        /* A zero-millisecond budget must stop the very first pass rather than run to completion. */
        auto options = iw::Options();
        options.max_arity = 1;
        options.max_time_in_ms = 0;

        const auto result = iw::find_solution(make_context("gripper/domain.pddl", "gripper/test_problem.pddl"), options);

        EXPECT_EQ(result.status, SearchStatus::OUT_OF_TIME);
        EXPECT_FALSE(result.plan.has_value());
    }

    {
        /* Likewise for the node budget -- and it must not be retried at a wider arity, which would
           spend the budget again for every remaining width. */
        auto options = iw::Options();
        options.max_arity = 3;
        options.max_num_states = 2;

        const auto result = iw::find_solution(make_context("gripper/domain.pddl", "gripper/test_problem.pddl"), options);

        EXPECT_EQ(result.status, SearchStatus::OUT_OF_STATES);
        EXPECT_FALSE(result.plan.has_value());
    }

    {
        /* The budget covers the whole search, not each arity pass afresh. With none of it left, no
           pass may be started at all -- not even to build its novelty table, which is O(|atoms|^k)
           and would otherwise run to completion with no clock anywhere in sight.
           `max_arity = 3` here so that "no pass ran" is a statement about three of them.

           Note what is deliberately not asserted: a wall-clock bound. How closely BrFS tracks its
           own deadline is BrFS's business and predates this forwarding -- it checks the clock once
           per node pop, so an instance with very expensive single expansions overshoots by however
           long one expansion takes, whether the budget arrives through `brfs::Options` directly or
           through `iw::Options`. What is new here is that the budget arrives at all. */
        auto options = iw::Options();
        options.max_arity = 3;
        options.max_time_in_ms = 0;

        const auto result = iw::find_solution(make_context("gripper/domain.pddl", "gripper/test_problem.pddl"), options);

        EXPECT_EQ(result.status, SearchStatus::OUT_OF_TIME);
        EXPECT_FALSE(result.plan.has_value());
    }

    {
        /* A budget large enough to finish must not change the answer. */
        auto options = iw::Options();
        options.max_arity = 1;
        options.max_time_in_ms = 60000;
        options.max_num_states = 1000000;

        const auto result = iw::find_solution(make_context("blocks_4/domain.pddl", "blocks_4/test_problem.pddl"), options);

        auto plain = iw::Options();
        plain.max_arity = 1;
        const auto reference = iw::find_solution(make_context("blocks_4/domain.pddl", "blocks_4/test_problem.pddl"), plain);

        EXPECT_EQ(result.status, reference.status);
    }

    {
        /* Left at their defaults, both must be exactly as invisible as before. */
        auto budgeted = iw::Options();
        budgeted.max_arity = 1;
        budgeted.max_time_in_ms = std::numeric_limits<uint32_t>::max();
        budgeted.max_num_states = std::numeric_limits<uint32_t>::max();
        const auto with_defaults = iw::find_solution(make_context("gripper/domain.pddl", "gripper/test_problem.pddl"), budgeted);

        auto plain = iw::Options();
        plain.max_arity = 1;
        const auto reference = iw::find_solution(make_context("gripper/domain.pddl", "gripper/test_problem.pddl"), plain);

        EXPECT_EQ(with_defaults.status, reference.status);
        EXPECT_EQ(with_defaults.plan.has_value(), reference.plan.has_value());
        if (reference.plan.has_value())
        {
            EXPECT_EQ(with_defaults.plan->get_actions().size(), reference.plan->get_actions().size());
        }
    }
}

/// A shared `SearchControl` lets a caller stop an IW search from another thread, and reports how far
/// it got. `brfs` refuses it on the paths whose layer bookkeeping cannot carry that meaning.
TEST(MimirTests, SearchAlgorithmsIWSearchControlTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "gripper/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl");

    const auto make_context = [&]
    { return SearchContextImpl::create(domain_file, problem_file, SearchContextImpl::Options(SearchContextImpl::GroundedOptions())); };

    {
        auto control = SearchControl();
        control.request_cancel();

        auto options = iw::Options();
        options.max_arity = 1;
        options.control = &control;

        const auto result = iw::find_solution(make_context(), options);

        EXPECT_EQ(result.status, SearchStatus::CANCELED);
    }

    {
        /* Every completed g-layer is published, and exhausting the width-1 space without a plan
           retracts the bound instead of raising it -- otherwise "my space is empty" would read as
           "the optimum is enormous" and certify whatever anyone else found. */
        auto control = SearchControl();

        auto options = iw::Options();
        options.max_arity = 1;
        options.control = &control;

        const auto result = iw::find_solution(make_context(), options);

        EXPECT_NE(result.status, SearchStatus::SOLVED) << "gripper's conjunctive goal has width 2";
        EXPECT_TRUE(control.is_lower_bound_invalidated());
        EXPECT_FALSE(control.is_incumbent_certified(1));
        EXPECT_GT(control.get_total_expansions(), 0u);
    }

    {
        /* Paths that do not expand each layer exhaustively must reject a control rather than publish
           a completed depth they cannot justify. */
        auto control = SearchControl();

        auto options = iw::Options();
        options.max_arity = 1;
        options.control = &control;
        options.beam_width = 2;
        options.layer_ordering_strategy = InOrderLayerOrderingStrategyImpl::create();

        EXPECT_THROW(iw::find_solution(make_context(), options), std::invalid_argument);
    }
}

}
