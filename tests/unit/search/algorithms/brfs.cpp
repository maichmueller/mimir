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

#include "mimir/search/algorithms/brfs.hpp"

#include "mimir/datasets/state_space.hpp"
#include "mimir/datasets/state_space_sampler.hpp"
#include "mimir/search/applicability.hpp"
#include "mimir/search/algorithms/strategies/layer_ordering_strategy.hpp"
#include "mimir/search/algorithms/strategies/goal_strategy.hpp"
#include "mimir/search/algorithms/strategies/pruning_strategy.hpp"
#include "mimir/search/algorithms/strategies/transition_ordering_strategy.hpp"
#include "mimir/search/algorithms/iw/pruning_strategy.hpp"
#include "mimir/search/landmarks/fact_landmark_generator.hpp"
#include "mimir/search/landmarks/fact_landmark_graph.hpp"
#include "mimir/formalism/action.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/predicate.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/algorithms.hpp"
#include "mimir/search/applicable_action_generators.hpp"
#include "mimir/search/axiom_evaluators.hpp"
#include "mimir/search/grounders.hpp"
#include "mimir/search/plan.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <gtest/gtest.h>

using namespace mimir::search;
using namespace mimir::formalism;

namespace mimir::tests
{

/// @brief Instantiate a lifted BrFS
class LiftedBrFSPlanner
{
private:
    Problem m_problem;
    KPKCLiftedApplicableActionGeneratorImpl::EventHandler m_applicable_action_generator_event_handler;
    KPKCLiftedApplicableActionGenerator m_applicable_action_generator;
    KPKCLiftedAxiomEvaluatorImpl::EventHandler m_axiom_evaluator_event_handler;
    KPKCLiftedAxiomEvaluator m_axiom_evaluator;
    StateRepository m_state_repository;
    brfs::EventHandler m_brfs_event_handler;
    SearchContext m_search_context;

public:
    LiftedBrFSPlanner(const fs::path& domain_file,
                     const fs::path& problem_file,
                     SearchContextImpl::SymmetryPruning symmetry_pruning = SearchContextImpl::SymmetryPruning::OFF) :
        m_problem(ProblemImpl::create(domain_file, problem_file)),
        m_applicable_action_generator_event_handler(KPKCLiftedApplicableActionGeneratorImpl::DefaultEventHandlerImpl::create()),
        m_applicable_action_generator(KPKCLiftedApplicableActionGeneratorImpl::create(m_problem,
                                                                                      SearchContextImpl::LiftedOptions::KPKCOptions(symmetry_pruning),
                                                                                      m_applicable_action_generator_event_handler)),
        m_axiom_evaluator_event_handler(KPKCLiftedAxiomEvaluatorImpl::DefaultEventHandlerImpl::create()),
        m_axiom_evaluator(KPKCLiftedAxiomEvaluatorImpl::create(m_problem, m_axiom_evaluator_event_handler)),
        m_state_repository(StateRepositoryImpl::create(m_axiom_evaluator)),
        m_brfs_event_handler(brfs::DefaultEventHandlerImpl::create(m_problem)),
        m_search_context(SearchContextImpl::create(m_problem, m_applicable_action_generator, m_state_repository))
    {
    }

    SearchResult find_solution()
    {
        auto brfs_options = brfs::Options();
        brfs_options.event_handler = m_brfs_event_handler;

        return brfs::find_solution(m_search_context, brfs_options);
    }

    const brfs::Statistics& get_algorithm_statistics() const { return m_brfs_event_handler->get_statistics(); }

    const KPKCLiftedApplicableActionGeneratorImpl::Statistics& get_applicable_action_generator_statistics() const
    {
        return m_applicable_action_generator_event_handler->get_statistics();
    }

    const KPKCLiftedAxiomEvaluatorImpl::Statistics& get_axiom_evaluator_statistics() const { return m_axiom_evaluator_event_handler->get_statistics(); }
    const Problem& get_problem() const { return m_problem; }
    const SearchContext& get_search_context() const { return m_search_context; }
};

/// @brief Instantiate a grounded BrFS
class GroundedBrFSPlanner
{
private:
    Problem m_problem;
    LiftedGrounder m_grounder;
    GroundedApplicableActionGeneratorImpl::EventHandler m_applicable_action_generator_event_handler;
    GroundedApplicableActionGenerator m_applicable_action_generator;
    GroundedAxiomEvaluatorImpl::EventHandler m_axiom_evaluator_event_handler;
    GroundedAxiomEvaluator m_axiom_evaluator;
    StateRepository m_state_repository;
    brfs::EventHandler m_brfs_event_handler;
    SearchContext m_search_context;

public:
    GroundedBrFSPlanner(const fs::path& domain_file, const fs::path& problem_file) :
        m_problem(ProblemImpl::create(domain_file, problem_file)),
        m_grounder(m_problem),
        m_applicable_action_generator_event_handler(GroundedApplicableActionGeneratorImpl::DefaultEventHandlerImpl::create()),
        m_applicable_action_generator(
            m_grounder.create_grounded_applicable_action_generator(match_tree::Options(), m_applicable_action_generator_event_handler)),
        m_axiom_evaluator_event_handler(GroundedAxiomEvaluatorImpl::DefaultEventHandlerImpl::create()),
        m_axiom_evaluator(m_grounder.create_grounded_axiom_evaluator(match_tree::Options(), m_axiom_evaluator_event_handler)),
        m_state_repository(StateRepositoryImpl::create(m_axiom_evaluator)),
        m_brfs_event_handler(brfs::DefaultEventHandlerImpl::create(m_problem)),
        m_search_context(SearchContextImpl::create(m_problem, m_applicable_action_generator, m_state_repository))
    {
    }

    SearchResult find_solution()
    {
        auto brfs_options = brfs::Options();
        brfs_options.event_handler = m_brfs_event_handler;

        return brfs::find_solution(m_search_context, brfs_options);
    }

    const brfs::Statistics& get_algorithm_statistics() const { return m_brfs_event_handler->get_statistics(); }

    const GroundedApplicableActionGeneratorImpl::Statistics& get_applicable_action_generator_statistics() const
    {
        return m_applicable_action_generator_event_handler->get_statistics();
    }

    const GroundedAxiomEvaluatorImpl::Statistics& get_axiom_evaluator_statistics() const { return m_axiom_evaluator_event_handler->get_statistics(); }

    const Problem& get_problem() const { return m_problem; }
    const SearchContext& get_search_context() const { return m_search_context; }
};

/// @brief A single (parent, action, successor) transition as reported by the BrFS event handler.
struct RecordedTransition
{
    Index parent_state_index;
    formalism::GroundAction action;
    Index successor_state_index;
};

class RecordingBrFSEventHandler : public brfs::EventHandlerBase<RecordingBrFSEventHandler>
{
private:
    std::optional<State> m_start_state;
    StateList m_root_generated_states;
    StateList m_expanded_states;
    std::vector<RecordedTransition> m_in_search_tree_transitions;
    std::vector<RecordedTransition> m_not_in_search_tree_transitions;

    friend class brfs::EventHandlerBase<RecordingBrFSEventHandler>;

    void on_expand_state_impl(const State& state) { m_expanded_states.push_back(state); }

    void on_expand_goal_state_impl(const State& state)
    {
        [[maybe_unused]] const auto& ignored_state = state;
    }

    void on_generate_state_impl(const State& state, formalism::GroundAction action, ContinuousCost action_cost, const State& successor_state)
    {
        [[maybe_unused]] const auto& ignored_state = state;
        [[maybe_unused]] const auto& ignored_action = action;
        [[maybe_unused]] const auto ignored_action_cost = action_cost;
        [[maybe_unused]] const auto& ignored_successor_state = successor_state;
    }

    void on_generate_state_in_search_tree_impl(const State& state,
                                               formalism::GroundAction action,
                                               ContinuousCost action_cost,
                                               const State& successor_state)
    {
        [[maybe_unused]] const auto ignored_action_cost = action_cost;

        m_in_search_tree_transitions.push_back(RecordedTransition { state.get_index(), action, successor_state.get_index() });

        if (m_start_state.has_value() && (state.get_index() == m_start_state->get_index()))
        {
            m_root_generated_states.push_back(successor_state);
        }
    }

    void on_generate_state_not_in_search_tree_impl(const State& state,
                                                   formalism::GroundAction action,
                                                   ContinuousCost action_cost,
                                                   const State& successor_state)
    {
        [[maybe_unused]] const auto ignored_action_cost = action_cost;

        m_not_in_search_tree_transitions.push_back(RecordedTransition { state.get_index(), action, successor_state.get_index() });
    }

    void on_finish_g_layer_impl(uint32_t g_value, uint64_t num_expanded_states, uint64_t num_generated_states)
    {
        [[maybe_unused]] const auto ignored_g_value = g_value;
        [[maybe_unused]] const auto ignored_num_expanded_states = num_expanded_states;
        [[maybe_unused]] const auto ignored_num_generated_states = num_generated_states;
    }

    void on_start_search_impl(const State& start_state) { m_start_state = start_state; }

    void on_end_search_impl(uint64_t num_reached_fluent_atoms,
                            uint64_t num_reached_derived_atoms,
                            uint64_t num_states,
                            uint64_t num_nodes,
                            uint64_t num_actions,
                            uint64_t num_axioms)
    {
        [[maybe_unused]] const auto ignored_num_reached_fluent_atoms = num_reached_fluent_atoms;
        [[maybe_unused]] const auto ignored_num_reached_derived_atoms = num_reached_derived_atoms;
        [[maybe_unused]] const auto ignored_num_states = num_states;
        [[maybe_unused]] const auto ignored_num_nodes = num_nodes;
        [[maybe_unused]] const auto ignored_num_actions = num_actions;
        [[maybe_unused]] const auto ignored_num_axioms = num_axioms;
    }

    void on_solved_impl(const Plan& plan)
    {
        [[maybe_unused]] const auto& ignored_plan = plan;
    }

    void on_unsolvable_impl() {}

    void on_exhausted_impl() {}

public:
    explicit RecordingBrFSEventHandler(formalism::Problem problem) : EventHandlerBase(problem, false) {}

    const StateList& get_root_generated_states() const { return m_root_generated_states; }
    const StateList& get_expanded_states() const { return m_expanded_states; }
    const std::vector<RecordedTransition>& get_in_search_tree_transitions() const { return m_in_search_tree_transitions; }
    const std::vector<RecordedTransition>& get_not_in_search_tree_transitions() const { return m_not_in_search_tree_transitions; }
};

class RecordingScoringLayerOrderingStrategy : public ILayerOrderingStrategy
{
private:
    mutable ContinuousCost m_next_score;
    mutable std::vector<Index> m_first_layer_scored_state_indices;

public:
    RecordingScoringLayerOrderingStrategy() : m_next_score(0.0), m_first_layer_scored_state_indices() {}

    bool supports_eager_scoring() const override { return true; }

    ContinuousCost score_state(const State& state, DiscreteCost g_value) const override
    {
        if (g_value == 1)
        {
            m_first_layer_scored_state_indices.push_back(state.get_index());
        }

        return ++m_next_score;
    }

    bool prefer_higher_scores() const override { return true; }

    void order_layer(StateList& states, DiscreteCost g_value) override
    {
        [[maybe_unused]] auto& ignored_states = states;
        [[maybe_unused]] const auto ignored_g_value = g_value;
    }

    const std::vector<Index>& get_first_layer_scored_state_indices() const { return m_first_layer_scored_state_indices; }
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

class UnsupportedBeamPruningStrategy : public IPruningStrategy
{
public:
    bool test_prune_initial_state(const State& state) override
    {
        [[maybe_unused]] const auto& ignored_state = state;
        return false;
    }

    bool test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ) override
    {
        [[maybe_unused]] const auto& ignored_state = state;
        [[maybe_unused]] const auto& ignored_succ_state = succ_state;
        return !is_new_succ;
    }
};

static std::vector<Index> get_state_indices(const StateList& states)
{
    auto indices = std::vector<Index> {};
    indices.reserve(states.size());

    for (const auto& state : states)
    {
        indices.push_back(state.get_index());
    }

    return indices;
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

static GroundConjunctiveCondition make_custom_ground_goal(const Problem& problem)
{
    auto* mutable_problem = const_cast<ProblemImpl*>(problem.get());
    auto& repositories = const_cast<Repositories&>(mutable_problem->get_repositories());
    const auto& static_literals = mutable_problem->get_static_initial_atoms();
    const auto& fluent_literals = mutable_problem->get_fluent_initial_atoms();

    if (static_literals.empty() || fluent_literals.empty())
    {
        throw std::runtime_error("Expected the test problem to provide at least one static and one fluent initial atom.");
    }

    auto custom_goal_literals = GroundLiteralLists<StaticTag, FluentTag, DerivedTag> {};

    boost::hana::at_key(custom_goal_literals, boost::hana::type<StaticTag> {})
        .push_back(repositories.get_or_create_ground_literal(true, static_literals.front()));
    boost::hana::at_key(custom_goal_literals, boost::hana::type<FluentTag> {})
        .push_back(repositories.get_or_create_ground_literal(true, fluent_literals.front()));

    return mutable_problem->get_or_create_ground_conjunctive_condition(std::move(custom_goal_literals), {});
}

struct BrFSRunTrace
{
    SearchStatus status;
    std::optional<Index> goal_state_index;
    std::vector<std::string> plan_action_signatures;
    std::vector<Index> root_generated_state_indices;
    std::vector<Index> expanded_state_indices;
    std::vector<uint64_t> num_generated_until_g_value;
    std::vector<uint64_t> num_expanded_until_g_value;
    std::vector<uint64_t> num_pruned_until_g_value;
};

static BrFSRunTrace make_brfs_run_trace(const SearchResult& result, const RecordingBrFSEventHandler& event_handler)
{
    const auto& statistics = event_handler.get_statistics();

    return BrFSRunTrace {
        result.status,
        result.goal_state.has_value() ? std::make_optional(result.goal_state->get_index()) : std::nullopt,
        result.plan.has_value() ? get_plan_action_signatures(*result.plan) : std::vector<std::string> {},
        get_state_indices(event_handler.get_root_generated_states()),
        get_state_indices(event_handler.get_expanded_states()),
        statistics.get_num_generated_until_g_value(),
        statistics.get_num_expanded_until_g_value(),
        statistics.get_num_pruned_until_g_value(),
    };
}

static void expect_brfs_run_traces_match(const BrFSRunTrace& lhs, const BrFSRunTrace& rhs)
{
    EXPECT_EQ(lhs.status, rhs.status);
    EXPECT_EQ(lhs.goal_state_index, rhs.goal_state_index);
    EXPECT_EQ(lhs.plan_action_signatures, rhs.plan_action_signatures);
    EXPECT_EQ(lhs.root_generated_state_indices, rhs.root_generated_state_indices);
    EXPECT_EQ(lhs.expanded_state_indices, rhs.expanded_state_indices);
    EXPECT_EQ(lhs.num_generated_until_g_value, rhs.num_generated_until_g_value);
    EXPECT_EQ(lhs.num_expanded_until_g_value, rhs.num_expanded_until_g_value);
    EXPECT_EQ(lhs.num_pruned_until_g_value, rhs.num_pruned_until_g_value);
}

static brfs::Options make_classical_iw1_brfs_options(const Problem& problem,
                                                     brfs::EventHandler event_handler,
                                                     bool incremental = true,
                                                     bool debug_crosscheck = true)
{
    const auto& ground_fluent_atom_repository =
        boost::hana::at_key(problem->get_repositories().get_hana_repositories(), boost::hana::type<GroundAtomImpl<FluentTag>> {});

    auto options = brfs::Options {};
    options.event_handler = std::move(event_handler);
    options.pruning_strategy = iw::ArityKNoveltyPruningStrategyImpl::create(1, ground_fluent_atom_repository.size());
    options.iw1_incremental_first_applicability = incremental;
    options.iw1_incremental_first_applicability_debug_crosscheck = debug_crosscheck;
    return options;
}

static brfs::Options make_projective_iw1_brfs_options(const Problem& problem,
                                                      brfs::EventHandler event_handler,
                                                      bool incremental = true,
                                                      bool debug_crosscheck = true)
{
    auto options = brfs::Options {};
    options.event_handler = std::move(event_handler);
    options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, true, false, false);
    options.iw1_incremental_first_applicability = incremental;
    options.iw1_incremental_first_applicability_debug_crosscheck = debug_crosscheck;
    return options;
}

static brfs::Options make_classical_iw1_beam_options(const Problem& problem,
                                                     brfs::EventHandler event_handler,
                                                     bool incremental = true,
                                                     bool debug_crosscheck = true,
                                                     bool add_effect_precheck = true)
{
    auto options = make_classical_iw1_brfs_options(problem, std::move(event_handler), incremental, debug_crosscheck);
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
    options.beam_width = 64;
    options.beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
    options.iw1_precheck_add_effect_novelty = add_effect_precheck;
    return options;
}

static brfs::Options make_projective_iw1_beam_options(const Problem& problem,
                                                      brfs::EventHandler event_handler,
                                                      bool incremental = true,
                                                      bool debug_crosscheck = true,
                                                      bool add_effect_precheck = true)
{
    auto options = make_projective_iw1_brfs_options(problem, std::move(event_handler), incremental, debug_crosscheck);
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
    options.beam_width = 64;
    options.beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
    options.iw1_precheck_add_effect_novelty = add_effect_precheck;
    return options;
}

TEST(MimirTests, SearchAlgorithmsBrFSProblemGoalStrategyCustomGroundConjunctiveConditionTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                    fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));

    const auto problem = brfs.get_problem();
    const auto custom_goal = make_custom_ground_goal(problem);
    const auto default_goal_strategy = ProblemGoalStrategyImpl::create(problem);
    const auto custom_goal_strategy = ProblemGoalStrategyImpl::create(problem, std::optional<GroundConjunctiveCondition>(custom_goal));

    ASSERT_TRUE(custom_goal_strategy->test_static_goal());

    const auto [initial_state, initial_metric_value] = brfs.get_search_context()->get_state_repository()->get_or_create_initial_state();
    [[maybe_unused]] const auto ignored_initial_metric_value = initial_metric_value;

    EXPECT_TRUE(custom_goal_strategy->test_dynamic_goal(initial_state));
    EXPECT_FALSE(default_goal_strategy->test_dynamic_goal(initial_state));
}

TEST(MimirTests, SearchAlgorithmsBrFSProblemMultiGoalStrategyMatchesDisjunctionOfConditionsTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                    fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));

    const auto problem = brfs.get_problem();
    const auto default_goal = problem->get_goal_condition();
    const auto custom_goal = make_custom_ground_goal(problem);
    const auto default_goal_strategy = ProblemGoalStrategyImpl::create(problem);
    const auto custom_goal_strategy = ProblemGoalStrategyImpl::create(problem, std::optional<GroundConjunctiveCondition>(custom_goal));
    const auto any_goal_strategy = ProblemMultiGoalStrategyImpl::create(problem, std::vector<GroundConjunctiveCondition> { default_goal, custom_goal });

    const auto [initial_state, initial_metric_value] = brfs.get_search_context()->get_state_repository()->get_or_create_initial_state();
    [[maybe_unused]] const auto ignored_initial_metric_value = initial_metric_value;

    EXPECT_EQ(any_goal_strategy->test_static_goal(), default_goal_strategy->test_static_goal() || custom_goal_strategy->test_static_goal());
    EXPECT_EQ(any_goal_strategy->test_dynamic_goal(initial_state), default_goal_strategy->test_dynamic_goal(initial_state) || custom_goal_strategy->test_dynamic_goal(initial_state));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Classical planning
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * agricola-opt18-strips
 *
 * IPC instances are too difficult
 */

/**
 * Airport
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedAssemblyTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "assembly/domain.pddl"), fs::path(std::string(DATA_DIR) + "assembly/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 1);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 3);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 1);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedAssemblyTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "assembly/domain.pddl"), fs::path(std::string(DATA_DIR) + "assembly/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 1);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 3);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 1);
}

/**
 * Airport
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedAirportTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "airport/domain.pddl"), fs::path(std::string(DATA_DIR) + "airport/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 8);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 20);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 18);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedAirportTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "airport/domain.pddl"), fs::path(std::string(DATA_DIR) + "airport/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 8);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 20);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 18);
}

/**
 * Barman
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedBarmanTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "barman/domain.pddl"), fs::path(std::string(DATA_DIR) + "barman/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 11);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 708);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 230);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedBarmanTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "barman/domain.pddl"), fs::path(std::string(DATA_DIR) + "barman/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 11);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 708);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 230);
}

/**
 * Blocks 3 ops
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedBlocks3opsTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "blocks_3/domain.pddl"), fs::path(std::string(DATA_DIR) + "blocks_3/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 68);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 21);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedBlocks3opsTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "blocks_3/domain.pddl"), fs::path(std::string(DATA_DIR) + "blocks_3/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 68);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 21);
}

/**
 * Blocks 4 ops
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedBlocks4opsTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "blocks_4/domain.pddl"), fs::path(std::string(DATA_DIR) + "blocks_4/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 21);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 9);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedBlocks4opsTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "blocks_4/domain.pddl"), fs::path(std::string(DATA_DIR) + "blocks_4/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 21);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 9);
}

/**
 * Childsnack
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedChildsnackTest)
{
    auto brfs =
        GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "childsnack/domain.pddl"), fs::path(std::string(DATA_DIR) + "childsnack/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 16);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 6);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedChildsnackTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "childsnack/domain.pddl"), fs::path(std::string(DATA_DIR) + "childsnack/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 16);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 6);
}

/**
 * Delivery
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedDeliveryTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 18);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 7);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedDeliveryTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 18);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 7);
}

/**
 * Driverlog
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedDriverlogTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "driverlog/domain.pddl"), fs::path(std::string(DATA_DIR) + "driverlog/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 9);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 57);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 23);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedDriverlogTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "driverlog/domain.pddl"), fs::path(std::string(DATA_DIR) + "driverlog/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 9);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 57);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 23);
}

/**
 * Ferry
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedFerryTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "ferry/domain.pddl"), fs::path(std::string(DATA_DIR) + "ferry/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 7);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 28);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 14);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedFerryTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "ferry/domain.pddl"), fs::path(std::string(DATA_DIR) + "ferry/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 7);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 28);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 14);
}

/**
 * Grid
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedGridTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "grid/domain.pddl"), fs::path(std::string(DATA_DIR) + "grid/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 18);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 7);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedGridTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "grid/domain.pddl"), fs::path(std::string(DATA_DIR) + "grid/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 18);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 7);
}

/**
 * Gripper
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedGripperTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 3);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 44);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 12);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedGripperTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 3);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 44);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 12);
}

TEST(MimirTests, SearchAlgorithmsBrFSDefaultLayerOrderingUsesGenerationOrder)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

    auto options = brfs::Options();
    options.event_handler = event_handler;
    options.stop_if_goal = false;

    const auto result = brfs::find_solution(brfs.get_search_context(), options);
    EXPECT_EQ(result.status, SearchStatus::EXHAUSTED);

    const auto generated_indices = get_state_indices(event_handler->get_root_generated_states());
    const auto expanded_indices = get_state_indices(event_handler->get_expanded_states());

    ASSERT_GE(generated_indices.size(), 2);
    ASSERT_GE(expanded_indices.size(), generated_indices.size() + 1);

    const auto actual_first_layer_indices =
        std::vector<Index>(expanded_indices.begin() + 1, expanded_indices.begin() + 1 + generated_indices.size());
    EXPECT_EQ(actual_first_layer_indices, generated_indices);
}

TEST(MimirTests, SearchAlgorithmsBrFSReverseLayerOrderingReordersFirstLayer)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

    auto options = brfs::Options();
    options.event_handler = event_handler;
    options.layer_ordering_strategy = ReverseOrderLayerOrderingStrategyImpl::create();
    options.stop_if_goal = false;

    const auto result = brfs::find_solution(brfs.get_search_context(), options);
    EXPECT_EQ(result.status, SearchStatus::EXHAUSTED);

    auto expected_indices = get_state_indices(event_handler->get_root_generated_states());
    const auto expanded_indices = get_state_indices(event_handler->get_expanded_states());

    ASSERT_GE(expected_indices.size(), 2);
    ASSERT_GE(expanded_indices.size(), expected_indices.size() + 1);

    std::reverse(expected_indices.begin(), expected_indices.end());
    const auto actual_first_layer_indices =
        std::vector<Index>(expanded_indices.begin() + 1, expanded_indices.begin() + 1 + expected_indices.size());
    EXPECT_EQ(actual_first_layer_indices, expected_indices);
}

TEST(MimirTests, SearchAlgorithmsBrFSMaxDepthZeroDoesNotGenerateSuccessorsTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
    auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

    auto options = brfs::Options();
    options.event_handler = event_handler;
    options.max_depth = 0;
    options.stop_if_goal = false;

    const auto result = brfs::find_solution(brfs.get_search_context(), options);
    EXPECT_EQ(result.status, SearchStatus::EXHAUSTED);
    EXPECT_FALSE(result.plan.has_value());
    EXPECT_TRUE(event_handler->get_root_generated_states().empty());

    const auto [start_state, _] = brfs.get_search_context()->get_state_repository()->get_or_create_initial_state();
    const auto expanded_indices = get_state_indices(event_handler->get_expanded_states());
    ASSERT_EQ(expanded_indices.size(), 1);
    EXPECT_EQ(expanded_indices.front(), start_state.get_index());
}

TEST(MimirTests, SearchAlgorithmsBrFSMaxDepthMatchesSolutionBoundaryOrderedLayerTest)
{
    auto shallow_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
    auto shallow_options = brfs::Options();
    shallow_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(shallow_brfs.get_problem());
    shallow_options.max_depth = 3;

    const auto shallow_result = brfs::find_solution(shallow_brfs.get_search_context(), shallow_options);
    EXPECT_EQ(shallow_result.status, SearchStatus::EXHAUSTED);
    EXPECT_FALSE(shallow_result.plan.has_value());

    auto exact_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
    auto exact_options = brfs::Options();
    exact_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(exact_brfs.get_problem());
    exact_options.max_depth = 4;

    const auto exact_result = brfs::find_solution(exact_brfs.get_search_context(), exact_options);
    ASSERT_EQ(exact_result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(exact_result.plan.has_value());
    EXPECT_EQ(exact_result.plan->get_actions().size(), 4);
}

TEST(MimirTests, SearchAlgorithmsBrFSMaxDepthMatchesSolutionBoundaryBeamTest)
{
    auto shallow_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
    auto shallow_options = brfs::Options();
    shallow_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(shallow_brfs.get_problem());
    shallow_options.beam_width = 64;
    shallow_options.max_depth = 3;

    const auto shallow_result = brfs::find_solution(shallow_brfs.get_search_context(), shallow_options);
    EXPECT_EQ(shallow_result.status, SearchStatus::EXHAUSTED);
    EXPECT_FALSE(shallow_result.plan.has_value());

    auto exact_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"), fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
    auto exact_options = brfs::Options();
    exact_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(exact_brfs.get_problem());
    exact_options.beam_width = 64;
    exact_options.max_depth = 4;

    const auto exact_result = brfs::find_solution(exact_brfs.get_search_context(), exact_options);
    ASSERT_EQ(exact_result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(exact_result.plan.has_value());
    EXPECT_EQ(exact_result.plan->get_actions().size(), 4);
}

TEST(MimirTests, SearchAlgorithmsBrFSNextLayerLimitRequiresLayerOrderingStrategy)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));

    auto options = brfs::Options();
    options.max_next_layer_states = 2;
    options.stop_if_goal = false;

    EXPECT_THROW(brfs::find_solution(brfs.get_search_context(), options), std::invalid_argument);
}

TEST(MimirTests, SearchAlgorithmsBrFSCappedLayerUsesEagerScoringAndStopsEarly)
{
    auto baseline_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto baseline_event_handler = std::make_shared<RecordingBrFSEventHandler>(baseline_brfs.get_problem());

    auto baseline_options = brfs::Options();
    baseline_options.event_handler = baseline_event_handler;
    baseline_options.stop_if_goal = false;

    const auto baseline_result = brfs::find_solution(baseline_brfs.get_search_context(), baseline_options);
    EXPECT_EQ(baseline_result.status, SearchStatus::EXHAUSTED);

    const auto baseline_generated_indices = get_state_indices(baseline_event_handler->get_root_generated_states());
    ASSERT_GT(baseline_generated_indices.size(), 2);

    auto capped_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto capped_event_handler = std::make_shared<RecordingBrFSEventHandler>(capped_brfs.get_problem());
    auto capped_strategy = std::make_shared<RecordingScoringLayerOrderingStrategy>();

    auto capped_options = brfs::Options();
    capped_options.event_handler = capped_event_handler;
    capped_options.layer_ordering_strategy = capped_strategy;
    capped_options.max_next_layer_states = 2;
    capped_options.stop_if_goal = false;

    const auto capped_result = brfs::find_solution(capped_brfs.get_search_context(), capped_options);
    EXPECT_EQ(capped_result.status, SearchStatus::EXHAUSTED);

    const auto capped_generated_indices = get_state_indices(capped_event_handler->get_root_generated_states());
    const auto capped_expanded_indices = get_state_indices(capped_event_handler->get_expanded_states());
    ASSERT_EQ(capped_generated_indices.size(), 2);
    EXPECT_EQ(capped_generated_indices,
              std::vector<Index>(baseline_generated_indices.begin(), baseline_generated_indices.begin() + capped_generated_indices.size()));
    EXPECT_EQ(capped_strategy->get_first_layer_scored_state_indices(), capped_generated_indices);

    ASSERT_GE(capped_expanded_indices.size(), 2);
    EXPECT_EQ(capped_expanded_indices[1], capped_generated_indices.back());
}

TEST(MimirTests, SearchAlgorithmsBrFSBeamWidthRequiresEagerScoringLayerOrderingStrategy)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));

    auto options = brfs::Options();
    options.layer_ordering_strategy = ReverseOrderLayerOrderingStrategyImpl::create();
    options.beam_width = 2;
    options.stop_if_goal = false;

    EXPECT_THROW(brfs::find_solution(brfs.get_search_context(), options), std::invalid_argument);
}

TEST(MimirTests, SearchAlgorithmsBrFSBeamWidthConflictsWithNextLayerLimit)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));

    auto options = brfs::Options();
    options.layer_ordering_strategy = std::make_shared<RecordingScoringLayerOrderingStrategy>();
    options.max_next_layer_states = 2;
    options.beam_width = 2;
    options.stop_if_goal = false;

    EXPECT_THROW(brfs::find_solution(brfs.get_search_context(), options), std::invalid_argument);
}

TEST(MimirTests, SearchAlgorithmsBrFSBeamSurvivorsOnlyRequiresSupportingPruningStrategy)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));

    auto options = brfs::Options();
    options.layer_ordering_strategy = std::make_shared<RecordingScoringLayerOrderingStrategy>();
    options.pruning_strategy = std::make_shared<UnsupportedBeamPruningStrategy>();
    options.beam_width = 2;
    options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
    options.stop_if_goal = false;

    EXPECT_THROW(brfs::find_solution(brfs.get_search_context(), options), std::invalid_argument);
}

TEST(MimirTests, SearchAlgorithmsBrFSBeamRetainsTopKScoredSuccessors)
{
    auto baseline_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto baseline_event_handler = std::make_shared<RecordingBrFSEventHandler>(baseline_brfs.get_problem());

    auto baseline_options = brfs::Options();
    baseline_options.event_handler = baseline_event_handler;
    baseline_options.stop_if_goal = false;

    const auto baseline_result = brfs::find_solution(baseline_brfs.get_search_context(), baseline_options);
    EXPECT_EQ(baseline_result.status, SearchStatus::EXHAUSTED);

    const auto baseline_generated_indices = get_state_indices(baseline_event_handler->get_root_generated_states());
    ASSERT_GT(baseline_generated_indices.size(), 2);

    auto beam_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto beam_event_handler = std::make_shared<RecordingBrFSEventHandler>(beam_brfs.get_problem());

    auto beam_options = brfs::Options();
    beam_options.event_handler = beam_event_handler;
    beam_options.layer_ordering_strategy = std::make_shared<RecordingScoringLayerOrderingStrategy>();
    beam_options.beam_width = 2;
    beam_options.stop_if_goal = false;

    const auto beam_result = brfs::find_solution(beam_brfs.get_search_context(), beam_options);
    EXPECT_EQ(beam_result.status, SearchStatus::EXHAUSTED);

    const auto beam_generated_indices = get_state_indices(beam_event_handler->get_root_generated_states());
    const auto beam_expanded_indices = get_state_indices(beam_event_handler->get_expanded_states());
    const auto expected_kept_indices =
        std::vector<Index>(baseline_generated_indices.end() - 2, baseline_generated_indices.end());
    auto expected_ordered_indices = expected_kept_indices;
    std::reverse(expected_ordered_indices.begin(), expected_ordered_indices.end());

    EXPECT_EQ(beam_generated_indices, expected_ordered_indices);
    ASSERT_GE(beam_expanded_indices.size(), expected_ordered_indices.size() + 1);
    EXPECT_EQ(std::vector<Index>(beam_expanded_indices.begin() + 1, beam_expanded_indices.begin() + 1 + expected_ordered_indices.size()),
              expected_ordered_indices);
}

TEST(MimirTests, SearchAlgorithmsBrFSBeamEqualScoreUsesGenerationOrderByDefault)
{
    auto baseline_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto baseline_event_handler = std::make_shared<RecordingBrFSEventHandler>(baseline_brfs.get_problem());

    auto baseline_options = brfs::Options();
    baseline_options.event_handler = baseline_event_handler;
    baseline_options.stop_if_goal = false;

    const auto baseline_result = brfs::find_solution(baseline_brfs.get_search_context(), baseline_options);
    EXPECT_EQ(baseline_result.status, SearchStatus::EXHAUSTED);

    const auto baseline_generated_indices = get_state_indices(baseline_event_handler->get_root_generated_states());
    ASSERT_GT(baseline_generated_indices.size(), 2);

    auto beam_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto beam_event_handler = std::make_shared<RecordingBrFSEventHandler>(beam_brfs.get_problem());

    auto beam_options = brfs::Options();
    beam_options.event_handler = beam_event_handler;
    beam_options.layer_ordering_strategy = std::make_shared<ConstantScoringLayerOrderingStrategy>();
    beam_options.beam_width = 2;
    beam_options.stop_if_goal = false;

    const auto beam_result = brfs::find_solution(beam_brfs.get_search_context(), beam_options);
    EXPECT_EQ(beam_result.status, SearchStatus::EXHAUSTED);

    const auto beam_generated_indices = get_state_indices(beam_event_handler->get_root_generated_states());
    const auto beam_expanded_indices = get_state_indices(beam_event_handler->get_expanded_states());
    const auto expected_indices = std::vector<Index>(baseline_generated_indices.begin(), baseline_generated_indices.begin() + 2);

    EXPECT_EQ(beam_generated_indices, expected_indices);
    ASSERT_GE(beam_expanded_indices.size(), expected_indices.size() + 1);
    EXPECT_EQ(std::vector<Index>(beam_expanded_indices.begin() + 1, beam_expanded_indices.begin() + 1 + expected_indices.size()),
              expected_indices);
}

TEST(MimirTests, SearchAlgorithmsBrFSBeamEqualScoreRandomTieBreakIsDeterministic)
{
    auto default_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto default_event_handler = std::make_shared<RecordingBrFSEventHandler>(default_brfs.get_problem());

    auto default_options = brfs::Options();
    default_options.event_handler = default_event_handler;
    default_options.layer_ordering_strategy = std::make_shared<ConstantScoringLayerOrderingStrategy>();
    default_options.beam_width = 4;
    default_options.stop_if_goal = false;

    const auto default_result = brfs::find_solution(default_brfs.get_search_context(), default_options);
    EXPECT_EQ(default_result.status, SearchStatus::EXHAUSTED);

    const auto default_generated_indices = get_state_indices(default_event_handler->get_root_generated_states());

    auto run_with_seed = [&](uint64_t seed)
    {
        auto beam_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
        auto beam_event_handler = std::make_shared<RecordingBrFSEventHandler>(beam_brfs.get_problem());

        auto beam_options = brfs::Options();
        beam_options.event_handler = beam_event_handler;
        beam_options.layer_ordering_strategy = std::make_shared<ConstantScoringLayerOrderingStrategy>();
        beam_options.beam_width = 4;
        beam_options.randomize_equal_score_ties = true;
        beam_options.equal_score_tie_seed = seed;
        beam_options.stop_if_goal = false;

        const auto beam_result = brfs::find_solution(beam_brfs.get_search_context(), beam_options);
        EXPECT_EQ(beam_result.status, SearchStatus::EXHAUSTED);

        const auto beam_generated_indices = get_state_indices(beam_event_handler->get_root_generated_states());
        const auto beam_expanded_indices = get_state_indices(beam_event_handler->get_expanded_states());
        const auto first_layer_expanded_indices =
            std::vector<Index>(beam_expanded_indices.begin() + 1, beam_expanded_indices.begin() + 1 + beam_generated_indices.size());
        return std::make_pair(beam_generated_indices, first_layer_expanded_indices);
    };

    const auto first_run = run_with_seed(7);
    const auto second_run = run_with_seed(7);

    EXPECT_EQ(first_run, second_run);
    EXPECT_NE(first_run.first, default_generated_indices);
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamRequiresBeamWidth)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));

    auto options = brfs::Options();
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
    options.parallel_beam_num_threads = 2;
    options.stop_if_goal = false;

    EXPECT_THROW(brfs::find_solution(brfs.get_search_context(), options), std::invalid_argument);
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamRejectsZeroChunkSize)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));

    auto options = brfs::Options();
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
    options.beam_width = 4;
    options.parallel_beam_num_threads = 2;
    options.parallel_beam_chunk_size = 0;
    options.stop_if_goal = false;

    EXPECT_THROW(brfs::find_solution(brfs.get_search_context(), options), std::invalid_argument);
}

TEST(MimirTests, SearchAlgorithmsBrFSRelaxedSurvivorsOnlyBeamRequiresSurvivorsOnlyMode)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                    fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));

    auto options = brfs::Options();
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
    options.pruning_strategy = DuplicatePruningStrategyImpl::create();
    options.beam_width = 8;
    options.beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
    options.parallel_beam_num_threads = 2;
    options.relaxed_survivors_only_beam = true;

    EXPECT_THROW(brfs::find_solution(brfs.get_search_context(), options), std::invalid_argument);
}

TEST(MimirTests, SearchAlgorithmsBrFSRelaxedSurvivorsOnlyBeamRequiresParallelThreads)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                    fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));

    auto options = brfs::Options();
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
    options.pruning_strategy = DuplicatePruningStrategyImpl::create();
    options.beam_width = 8;
    options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
    options.parallel_beam_num_threads = 1;
    options.relaxed_survivors_only_beam = true;

    EXPECT_THROW(brfs::find_solution(brfs.get_search_context(), options), std::invalid_argument);
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelAllTestedBeamMatchesSerialTest)
{
    auto serial_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto serial_event_handler = std::make_shared<RecordingBrFSEventHandler>(serial_brfs.get_problem());
    auto serial_options = brfs::Options();
    serial_options.event_handler = serial_event_handler;
    serial_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(serial_brfs.get_problem());
    serial_options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(serial_brfs.get_problem(), 1, true, false, true);
    serial_options.beam_width = 4;
    serial_options.beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
    serial_options.stop_if_goal = false;
    const auto serial_result = brfs::find_solution(serial_brfs.get_search_context(), serial_options);

    auto parallel_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto parallel_event_handler = std::make_shared<RecordingBrFSEventHandler>(parallel_brfs.get_problem());
    auto parallel_options = serial_options;
    parallel_options.event_handler = parallel_event_handler;
    parallel_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(parallel_brfs.get_problem());
    parallel_options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(parallel_brfs.get_problem(), 1, true, false, true);
    parallel_options.parallel_beam_num_threads = 2;
    const auto parallel_result = brfs::find_solution(parallel_brfs.get_search_context(), parallel_options);

    EXPECT_EQ(parallel_result.status, serial_result.status);
    EXPECT_EQ(get_state_indices(parallel_event_handler->get_root_generated_states()), get_state_indices(serial_event_handler->get_root_generated_states()));
    EXPECT_EQ(get_state_indices(parallel_event_handler->get_expanded_states()), get_state_indices(serial_event_handler->get_expanded_states()));
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamCustomChunkSizesMatchSerialTest)
{
    auto run = [](uint32_t chunk_size)
    {
        auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                        fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());
        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
        options.beam_width = 64;
        options.beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
        options.parallel_beam_num_threads = (chunk_size > 0) ? 2 : 1;
        options.parallel_beam_chunk_size = (chunk_size > 0) ? chunk_size : options.parallel_beam_chunk_size;
        options.stop_if_goal = true;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        return std::make_pair(make_brfs_run_trace(result, *event_handler), event_handler->get_statistics());
    };

    const auto [serial_trace, serial_stats] = run(0);
    [[maybe_unused]] const auto ignored_serial_stats = serial_stats;

    for (const auto chunk_size : { 1u, 4096u })
    {
        SCOPED_TRACE(chunk_size);
        const auto [parallel_trace, parallel_stats] = run(chunk_size);
        expect_brfs_run_traces_match(parallel_trace, serial_trace);
        EXPECT_GT(parallel_stats.get_num_parallel_beam_chunk_flushes(), 0);
        EXPECT_LE(parallel_stats.get_max_parallel_beam_chunk_size(), chunk_size);
    }
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamInternSubphasesAreMeasuredTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "schedule/domain.pddl"),
                                    fs::path(std::string(DATA_DIR) + "schedule/test_problem.pddl"));
    auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

    auto options = brfs::Options();
    options.event_handler = event_handler;
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
    options.beam_width = 256;
    options.beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
    options.parallel_beam_num_threads = 4;
    options.parallel_beam_chunk_size = 512;

    const auto result = brfs::find_solution(brfs.get_search_context(), options);
    EXPECT_EQ(result.status, SearchStatus::SOLVED);

    const auto& statistics = event_handler->get_statistics();
    EXPECT_GT(statistics.get_num_parallel_beam_chunk_flushes(), 0);
    EXPECT_GT(statistics.get_parallel_beam_worker_compute_time_ms(), 0.0);
    EXPECT_GT(statistics.get_parallel_beam_main_thread_merge_time_ms(), 0.0);
    EXPECT_GT(statistics.get_parallel_beam_main_thread_intern_time_ms(), 0.0);
    EXPECT_GT(statistics.get_parallel_beam_fluent_slot_time_ms(), 0.0);
    EXPECT_GT(statistics.get_parallel_beam_numeric_slot_time_ms(), 0.0);
    EXPECT_GT(statistics.get_parallel_beam_state_lookup_time_ms(), 0.0);
    EXPECT_GT(statistics.get_parallel_beam_ready_queue_high_water(), 0u);
    EXPECT_GT(statistics.get_parallel_beam_in_flight_chunks_high_water(), 0u);
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamMatchesSerialLiftedKPKCTest)
{
    auto run = [](const std::string& domain_name,
                  const std::string& problem_name,
                  SearchContextImpl::SymmetryPruning symmetry_pruning,
                  BeamNoveltyMode beam_novelty_mode,
                  uint32_t parallel_threads)
    {
        auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + domain_name + "/domain.pddl"),
                                      fs::path(std::string(DATA_DIR) + domain_name + "/" + problem_name),
                                      symmetry_pruning);
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
        options.pruning_strategy = DuplicatePruningStrategyImpl::create();
        options.beam_width = 64;
        options.beam_novelty_mode = beam_novelty_mode;
        options.parallel_beam_num_threads = parallel_threads;
        options.stop_if_goal = true;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(brfs.get_search_context(), result);
        }

        return make_brfs_run_trace(result, *event_handler);
    };

    for (const auto& [domain_name, problem_name] :
         std::vector<std::pair<std::string, std::string>> { { "delivery", "test_problem.pddl" }, { "philosophers", "test_problem.pddl" } })
    {
        SCOPED_TRACE(domain_name);

        for (const auto beam_novelty_mode : { BeamNoveltyMode::ALL_TESTED, BeamNoveltyMode::SURVIVORS_ONLY })
        {
            SCOPED_TRACE(beam_novelty_mode == BeamNoveltyMode::ALL_TESTED ? "all_tested" : "survivors_only");
            const auto serial_trace = run(domain_name, problem_name, SearchContextImpl::SymmetryPruning::OFF, beam_novelty_mode, 1);

            for (const auto parallel_threads : { 2u, 4u })
            {
                SCOPED_TRACE(parallel_threads);
                expect_brfs_run_traces_match(
                    run(domain_name, problem_name, SearchContextImpl::SymmetryPruning::OFF, beam_novelty_mode, parallel_threads),
                    serial_trace);
            }
        }
    }
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamMatchesSerialLiftedSymmetryPruningTest)
{
    auto run = [](BeamNoveltyMode beam_novelty_mode, uint32_t parallel_threads)
    {
        auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                      fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"),
                                      SearchContextImpl::SymmetryPruning::GI);
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
        options.pruning_strategy = DuplicatePruningStrategyImpl::create();
        options.beam_width = 64;
        options.beam_novelty_mode = beam_novelty_mode;
        options.parallel_beam_num_threads = parallel_threads;
        options.stop_if_goal = true;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(brfs.get_search_context(), result);
        }

        return make_brfs_run_trace(result, *event_handler);
    };

    for (const auto beam_novelty_mode : { BeamNoveltyMode::ALL_TESTED, BeamNoveltyMode::SURVIVORS_ONLY })
    {
        SCOPED_TRACE(beam_novelty_mode == BeamNoveltyMode::ALL_TESTED ? "all_tested" : "survivors_only");
        const auto serial_trace = run(beam_novelty_mode, 1);
        for (const auto parallel_threads : { 2u, 4u })
        {
            SCOPED_TRACE(parallel_threads);
            expect_brfs_run_traces_match(run(beam_novelty_mode, parallel_threads), serial_trace);
        }
    }
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelProjectiveBeamMatchesSerialLiftedKPKCTest)
{
    auto run = [](const std::string& domain_name,
                  const std::string& problem_name,
                  SearchContextImpl::SymmetryPruning symmetry_pruning,
                  BeamNoveltyMode beam_novelty_mode,
                  uint32_t parallel_threads)
    {
        auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + domain_name + "/domain.pddl"),
                                      fs::path(std::string(DATA_DIR) + domain_name + "/" + problem_name),
                                      symmetry_pruning);
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
        options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(brfs.get_problem(), 1, true, false, true);
        options.beam_width = 64;
        options.beam_novelty_mode = beam_novelty_mode;
        options.parallel_beam_num_threads = parallel_threads;
        options.stop_if_goal = false;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(brfs.get_search_context(), result);
        }

        return make_brfs_run_trace(result, *event_handler);
    };

    for (const auto& [domain_name, problem_name, symmetry_pruning] :
         std::vector<std::tuple<std::string, std::string, SearchContextImpl::SymmetryPruning>> {
             { "delivery", "test_problem.pddl", SearchContextImpl::SymmetryPruning::OFF },
             { "philosophers", "test_problem.pddl", SearchContextImpl::SymmetryPruning::OFF },
             { "delivery", "test_problem.pddl", SearchContextImpl::SymmetryPruning::GI } })
    {
        SCOPED_TRACE(domain_name);
        SCOPED_TRACE(symmetry_pruning == SearchContextImpl::SymmetryPruning::GI ? "symmetry_pruning" : "plain_lifted");

        for (const auto beam_novelty_mode : { BeamNoveltyMode::ALL_TESTED, BeamNoveltyMode::SURVIVORS_ONLY })
        {
            SCOPED_TRACE(beam_novelty_mode == BeamNoveltyMode::ALL_TESTED ? "all_tested" : "survivors_only");
            const auto serial_trace = run(domain_name, problem_name, symmetry_pruning, beam_novelty_mode, 1);

            for (const auto parallel_threads : { 2u, 4u })
            {
                SCOPED_TRACE(parallel_threads);
                expect_brfs_run_traces_match(run(domain_name, problem_name, symmetry_pruning, beam_novelty_mode, parallel_threads),
                                             serial_trace);
            }
        }
    }
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamTieBreakingMatchesSerialLiftedKPKCTest)
{
    auto run = [](bool randomize_equal_score_ties, uint64_t seed, uint32_t parallel_threads)
    {
        auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                      fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = std::make_shared<ConstantScoringLayerOrderingStrategy>();
        options.pruning_strategy = DuplicatePruningStrategyImpl::create();
        options.beam_width = 8;
        options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
        options.randomize_equal_score_ties = randomize_equal_score_ties;
        options.equal_score_tie_seed = seed;
        options.parallel_beam_num_threads = parallel_threads;
        options.stop_if_goal = false;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        return make_brfs_run_trace(result, *event_handler);
    };

    for (const auto parallel_threads : { 2u, 4u })
    {
        const auto default_first = run(false, 0, parallel_threads);
        const auto default_second = run(false, 0, parallel_threads);
        const auto seeded_first = run(true, 7, parallel_threads);
        const auto seeded_second = run(true, 7, parallel_threads);

        expect_brfs_run_traces_match(default_first, default_second);
        expect_brfs_run_traces_match(seeded_first, seeded_second);
    }
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamRepeatedMatchesSerialLiftedKPKCTest)
{
    auto run = [](const std::string& domain_name, BeamNoveltyMode beam_novelty_mode, uint32_t parallel_threads)
    {
        auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + domain_name + "/domain.pddl"),
                                      fs::path(std::string(DATA_DIR) + domain_name + "/test_problem.pddl"));
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
        options.pruning_strategy = DuplicatePruningStrategyImpl::create();
        options.beam_width = 64;
        options.beam_novelty_mode = beam_novelty_mode;
        options.parallel_beam_num_threads = parallel_threads;
        options.stop_if_goal = true;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(brfs.get_search_context(), result);
        }

        return make_brfs_run_trace(result, *event_handler);
    };

    for (const auto& domain_name : { std::string("delivery"), std::string("philosophers") })
    {
        SCOPED_TRACE(domain_name);

        for (const auto beam_novelty_mode : { BeamNoveltyMode::ALL_TESTED, BeamNoveltyMode::SURVIVORS_ONLY })
        {
            SCOPED_TRACE(beam_novelty_mode == BeamNoveltyMode::ALL_TESTED ? "all_tested" : "survivors_only");
            const auto serial_trace = run(domain_name, beam_novelty_mode, 1);

            for (int repetition = 0; repetition < 20; ++repetition)
            {
                SCOPED_TRACE(repetition);
                for (const auto parallel_threads : { 2u, 4u })
                {
                    SCOPED_TRACE(parallel_threads);
                    expect_brfs_run_traces_match(run(domain_name, beam_novelty_mode, parallel_threads), serial_trace);
                }
            }
        }
    }
}

TEST(MimirTests, SearchAlgorithmsBrFSReleaseParallelMemoryKeepsLiftedParallelBeamBehaviorTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                  fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));

    auto run = [&](uint32_t parallel_threads)
    {
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
        options.pruning_strategy = DuplicatePruningStrategyImpl::create();
        options.beam_width = 64;
        options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
        options.parallel_beam_num_threads = parallel_threads;
        options.stop_if_goal = true;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(brfs.get_search_context(), result);
        }

        return make_brfs_run_trace(result, *event_handler);
    };

    const auto first_trace = run(4);
    brfs.get_search_context()->release_parallel_memory(false);
    const auto second_trace = run(4);
    brfs.get_search_context()->release_parallel_memory(true);
    const auto third_trace = run(4);

    expect_brfs_run_traces_match(second_trace, first_trace);
    expect_brfs_run_traces_match(third_trace, first_trace);
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamRejectsLiftedExhaustiveContexts)
{
    auto problem = ProblemImpl::create(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                       fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
    auto search_context = SearchContextImpl::create(problem,
                                                    SearchContextImpl::Options(
                                                        SearchContextImpl::LiftedOptions(SearchContextImpl::LiftedOptions::ExhaustiveOptions())));

    auto options = brfs::Options();
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
    options.pruning_strategy = DuplicatePruningStrategyImpl::create();
    options.beam_width = 8;
    options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
    options.parallel_beam_num_threads = 2;

    try
    {
        [[maybe_unused]] const auto result = brfs::find_solution(search_context, options);
        FAIL() << "Expected invalid_argument";
    }
    catch (const std::invalid_argument& error)
    {
        EXPECT_NE(std::string(error.what()).find("lifted exhaustive"), std::string::npos);
    }
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamThreadsOneUsesSerialPath)
{
    auto serial_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto serial_event_handler = std::make_shared<RecordingBrFSEventHandler>(serial_brfs.get_problem());
    auto serial_options = brfs::Options();
    serial_options.event_handler = serial_event_handler;
    serial_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(serial_brfs.get_problem());
    serial_options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(serial_brfs.get_problem(), 1, true, false, true);
    serial_options.beam_width = 4;
    serial_options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
    serial_options.stop_if_goal = false;
    const auto serial_result = brfs::find_solution(serial_brfs.get_search_context(), serial_options);

    auto one_thread_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto one_thread_event_handler = std::make_shared<RecordingBrFSEventHandler>(one_thread_brfs.get_problem());
    auto one_thread_options = serial_options;
    one_thread_options.event_handler = one_thread_event_handler;
    one_thread_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(one_thread_brfs.get_problem());
    one_thread_options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(one_thread_brfs.get_problem(), 1, true, false, true);
    one_thread_options.parallel_beam_num_threads = 1;
    const auto one_thread_result = brfs::find_solution(one_thread_brfs.get_search_context(), one_thread_options);

    EXPECT_EQ(one_thread_result.status, serial_result.status);
    EXPECT_EQ(get_state_indices(one_thread_event_handler->get_root_generated_states()), get_state_indices(serial_event_handler->get_root_generated_states()));
    EXPECT_EQ(get_state_indices(one_thread_event_handler->get_expanded_states()), get_state_indices(serial_event_handler->get_expanded_states()));
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelProjectiveBeamMatchesSerialTest)
{
    auto serial_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto serial_event_handler = std::make_shared<RecordingBrFSEventHandler>(serial_brfs.get_problem());
    auto serial_options = brfs::Options();
    serial_options.event_handler = serial_event_handler;
    serial_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(serial_brfs.get_problem());
    serial_options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(serial_brfs.get_problem(), 1, true, true, true);
    serial_options.beam_width = 4;
    serial_options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
    serial_options.stop_if_goal = false;
    const auto serial_result = brfs::find_solution(serial_brfs.get_search_context(), serial_options);

    auto parallel_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto parallel_event_handler = std::make_shared<RecordingBrFSEventHandler>(parallel_brfs.get_problem());
    auto parallel_options = serial_options;
    parallel_options.event_handler = parallel_event_handler;
    parallel_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(parallel_brfs.get_problem());
    parallel_options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(parallel_brfs.get_problem(), 1, true, true, true);
    parallel_options.parallel_beam_num_threads = 2;
    const auto parallel_result = brfs::find_solution(parallel_brfs.get_search_context(), parallel_options);

    EXPECT_EQ(parallel_result.status, serial_result.status);
    EXPECT_EQ(get_state_indices(parallel_event_handler->get_root_generated_states()), get_state_indices(serial_event_handler->get_root_generated_states()));
    EXPECT_EQ(get_state_indices(parallel_event_handler->get_expanded_states()), get_state_indices(serial_event_handler->get_expanded_states()));
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamTieBreakingMatchesSerialTest)
{
    auto run = [](bool randomize_equal_score_ties, uint64_t seed, uint32_t parallel_threads)
    {
        auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = std::make_shared<ConstantScoringLayerOrderingStrategy>();
        options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(brfs.get_problem(), 1, true, false, true);
        options.beam_width = 4;
        options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
        options.randomize_equal_score_ties = randomize_equal_score_ties;
        options.equal_score_tie_seed = seed;
        options.parallel_beam_num_threads = parallel_threads;
        options.stop_if_goal = false;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        EXPECT_EQ(result.status, SearchStatus::EXHAUSTED);
        return std::make_pair(get_state_indices(event_handler->get_root_generated_states()), get_state_indices(event_handler->get_expanded_states()));
    };

    for (const auto parallel_threads : { 2u, 4u, 8u })
    {
        EXPECT_EQ(run(false, 0, parallel_threads), run(false, 0, parallel_threads));
        EXPECT_EQ(run(true, 7, parallel_threads), run(true, 7, parallel_threads));
    }
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamDuplicatePruningCoversFullStateSpaceTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "blocks_4/domain.pddl"), fs::path(std::string(DATA_DIR) + "blocks_4/test_problem.pddl"));
    auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

    const auto state_space_result = datasets::StateSpaceImpl::create(brfs.get_search_context());
    ASSERT_TRUE(state_space_result.has_value());
    const auto& state_space = state_space_result->first;
    auto state_space_sampler = datasets::StateSpaceSamplerImpl(state_space);

    auto expected_state_indices = std::vector<Index> {};
    expected_state_indices.reserve(state_space->get_graph().get_num_vertices());
    for (const auto& vertex : state_space->get_graph().get_vertices())
    {
        expected_state_indices.push_back(graphs::get_state(vertex).get_index());
    }
    std::sort(expected_state_indices.begin(), expected_state_indices.end());

    auto options = brfs::Options();
    options.event_handler = event_handler;
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
    options.beam_width = static_cast<uint32_t>(state_space->get_graph().get_num_vertices());
    options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
    options.parallel_beam_num_threads = 2;
    options.stop_if_goal = false;

    const auto result = brfs::find_solution(brfs.get_search_context(), options);
    EXPECT_EQ(result.status, SearchStatus::EXHAUSTED);

    auto actual_state_indices = get_state_indices(event_handler->get_expanded_states());
    std::sort(actual_state_indices.begin(), actual_state_indices.end());
    EXPECT_EQ(actual_state_indices, expected_state_indices);

    auto& applicable_action_generator = *brfs.get_search_context()->get_applicable_action_generator();
    auto& state_repository = *brfs.get_search_context()->get_state_repository();
    for (const auto& vertex : state_space->get_graph().get_vertices())
    {
        const auto state = graphs::get_state(vertex);

        auto expected_transitions = std::vector<std::pair<Index, Index>> {};
        for (const auto& [action, successor_state] : state_space_sampler.get_forward_transitions(state))
        {
            expected_transitions.emplace_back(action->get_index(), successor_state.get_index());
        }
        std::sort(expected_transitions.begin(), expected_transitions.end());

        auto actual_transitions = std::vector<std::pair<Index, Index>> {};
        for (const auto& action : applicable_action_generator.create_applicable_action_generator(state))
        {
            const auto [successor_state, successor_metric_value] = state_repository.get_or_create_successor_state(state, action, 0.0);
            [[maybe_unused]] const auto ignored_successor_metric_value = successor_metric_value;
            actual_transitions.emplace_back(action->get_index(), successor_state.get_index());
        }
        std::sort(actual_transitions.begin(), actual_transitions.end());

        EXPECT_EQ(actual_transitions, expected_transitions);
    }
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamGoalStopMatchesSerialDeliveryTest)
{
    auto run = [](uint32_t parallel_threads)
    {
        auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                        fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
        options.beam_width = 64;
        options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
        options.parallel_beam_num_threads = parallel_threads;
        options.stop_if_goal = true;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(brfs.get_search_context(), result);
        }

        return make_brfs_run_trace(result, *event_handler);
    };

    const auto serial_trace = run(1);
    const auto parallel_trace = run(4);

    expect_brfs_run_traces_match(parallel_trace, serial_trace);
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamRepeatedMatchesSerialDeliveryTest)
{
    auto run = [](BeamNoveltyMode beam_novelty_mode, uint32_t parallel_threads)
    {
        auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                        fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
        options.beam_width = 64;
        options.beam_novelty_mode = beam_novelty_mode;
        options.parallel_beam_num_threads = parallel_threads;
        options.stop_if_goal = true;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(brfs.get_search_context(), result);
        }

        return make_brfs_run_trace(result, *event_handler);
    };

    for (const auto beam_novelty_mode : { BeamNoveltyMode::ALL_TESTED, BeamNoveltyMode::SURVIVORS_ONLY })
    {
        SCOPED_TRACE(beam_novelty_mode == BeamNoveltyMode::ALL_TESTED ? "all_tested" : "survivors_only");

        const auto serial_trace = run(beam_novelty_mode, 1);

        for (int repetition = 0; repetition < 8; ++repetition)
        {
            SCOPED_TRACE(repetition);

            for (const auto parallel_threads : { 2u, 4u, 8u })
            {
                SCOPED_TRACE(parallel_threads);
                expect_brfs_run_traces_match(run(beam_novelty_mode, parallel_threads), serial_trace);
            }
        }
    }
}

TEST(MimirTests, SearchAlgorithmsBrFSRelaxedSurvivorsOnlyBeamWideBeamMatchesDeterministicDeliveryTest)
{
    auto run = [](bool relaxed_survivors_only_beam)
    {
        auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                        fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

        const auto state_space_result = datasets::StateSpaceImpl::create(brfs.get_search_context());
        EXPECT_TRUE(state_space_result.has_value());
        if (!state_space_result.has_value())
        {
            return BrFSRunTrace {};
        }
        const auto& state_space = state_space_result->first;

        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
        options.pruning_strategy = DuplicatePruningStrategyImpl::create();
        options.beam_width = static_cast<uint32_t>(state_space->get_graph().get_num_vertices());
        options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
        options.parallel_beam_num_threads = 4;
        options.relaxed_survivors_only_beam = relaxed_survivors_only_beam;
        options.stop_if_goal = false;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        EXPECT_EQ(result.status, SearchStatus::EXHAUSTED);
        return make_brfs_run_trace(result, *event_handler);
    };

    expect_brfs_run_traces_match(run(true), run(false));
}

TEST(MimirTests, SearchAlgorithmsBrFSRelaxedSurvivorsOnlyBeamWideBeamCoversLiftedDeliveryStateSpaceTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                  fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
    auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

    const auto state_space_result = datasets::StateSpaceImpl::create(brfs.get_search_context());
    ASSERT_TRUE(state_space_result.has_value());
    const auto& state_space = state_space_result->first;

    auto expected_state_indices = std::vector<Index> {};
    expected_state_indices.reserve(state_space->get_graph().get_num_vertices());
    for (const auto& vertex : state_space->get_graph().get_vertices())
    {
        expected_state_indices.push_back(graphs::get_state(vertex).get_index());
    }
    std::sort(expected_state_indices.begin(), expected_state_indices.end());

    auto options = brfs::Options();
    options.event_handler = event_handler;
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
    options.pruning_strategy = DuplicatePruningStrategyImpl::create();
    options.beam_width = static_cast<uint32_t>(state_space->get_graph().get_num_vertices());
    options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
    options.parallel_beam_num_threads = 4;
    options.relaxed_survivors_only_beam = true;
    options.stop_if_goal = false;

    const auto result = brfs::find_solution(brfs.get_search_context(), options);
    EXPECT_EQ(result.status, SearchStatus::EXHAUSTED);

    auto actual_state_indices = get_state_indices(event_handler->get_expanded_states());
    std::sort(actual_state_indices.begin(), actual_state_indices.end());
    EXPECT_EQ(actual_state_indices, expected_state_indices);
}

TEST(MimirTests, SearchAlgorithmsBrFSRelaxedSurvivorsOnlyBeamRepeatedLiftedKPKCTest)
{
    auto run = [](const std::string& domain_name, bool projective)
    {
        auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + domain_name + "/domain.pddl"),
                                      fs::path(std::string(DATA_DIR) + domain_name + "/test_problem.pddl"));
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
        options.pruning_strategy = projective ? iw::AbstractedNoveltyPruningStrategyImpl::create(brfs.get_problem(), 1, true, false, true) :
                                               DuplicatePruningStrategyImpl::create();
        options.beam_width = 64;
        options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
        options.parallel_beam_num_threads = 4;
        options.relaxed_survivors_only_beam = true;
        options.stop_if_goal = true;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(brfs.get_search_context(), result);
        }

        return make_brfs_run_trace(result, *event_handler);
    };

    for (const auto& [domain_name, projective] :
         std::vector<std::pair<std::string, bool>> { { "delivery", false }, { "delivery", true }, { "philosophers", true } })
    {
        SCOPED_TRACE(domain_name);
        SCOPED_TRACE(projective ? "projective" : "duplicate");
        const auto reference_trace = run(domain_name, projective);

        for (int repetition = 0; repetition < 8; ++repetition)
        {
            SCOPED_TRACE(repetition);
            expect_brfs_run_traces_match(run(domain_name, projective), reference_trace);
        }
    }
}


TEST(MimirTests, SearchAlgorithmsBrFSParallelProjectiveBeamOptionMatrixMatchesSerialTest)
{
    auto run = [](bool typed_projection,
                  bool keep_depth_one_novel,
                  bool keep_goal_nonunary_atoms,
                  BeamNoveltyMode beam_novelty_mode,
                  uint32_t parallel_threads)
    {
        auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "driverlog/domain.pddl"),
                                        fs::path(std::string(DATA_DIR) + "driverlog/test_problem.pddl"));
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
        options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(brfs.get_problem(),
                                                                                         1,
                                                                                         !typed_projection,
                                                                                         keep_goal_nonunary_atoms,
                                                                                         keep_depth_one_novel);
        options.beam_width = 64;
        options.beam_novelty_mode = beam_novelty_mode;
        options.parallel_beam_num_threads = parallel_threads;
        options.stop_if_goal = true;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(brfs.get_search_context(), result);
        }

        return make_brfs_run_trace(result, *event_handler);
    };

    for (const auto typed_projection : { false, true })
    {
        for (const auto keep_depth_one_novel : { false, true })
        {
            for (const auto keep_goal_nonunary_atoms : { false, true })
            {
                for (const auto beam_novelty_mode : { BeamNoveltyMode::ALL_TESTED, BeamNoveltyMode::SURVIVORS_ONLY })
                {
                    SCOPED_TRACE(typed_projection);
                    SCOPED_TRACE(keep_depth_one_novel);
                    SCOPED_TRACE(keep_goal_nonunary_atoms);
                    SCOPED_TRACE(beam_novelty_mode == BeamNoveltyMode::ALL_TESTED ? "all_tested" : "survivors_only");

                    const auto serial_trace =
                        run(typed_projection, keep_depth_one_novel, keep_goal_nonunary_atoms, beam_novelty_mode, 1);

                    for (const auto parallel_threads : { 2u, 4u })
                    {
                        SCOPED_TRACE(parallel_threads);
                        expect_brfs_run_traces_match(run(typed_projection,
                                                         keep_depth_one_novel,
                                                         keep_goal_nonunary_atoms,
                                                         beam_novelty_mode,
                                                         parallel_threads),
                                                     serial_trace);
                    }
                }
            }
        }
    }
}

TEST(MimirTests, SearchAlgorithmsBrFSProjectiveIW1PrecheckAndAtomFirstMatchBaselineTest)
{
    auto run = [](BeamNoveltyMode beam_novelty_mode, uint32_t parallel_threads, bool atom_first_mode)
    {
        auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "driverlog/domain.pddl"),
                                        fs::path(std::string(DATA_DIR) + "driverlog/test_problem.pddl"));
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
        options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(brfs.get_problem(), 1, true, false, true);
        options.beam_width = 64;
        options.beam_novelty_mode = beam_novelty_mode;
        options.parallel_beam_num_threads = parallel_threads;
        options.stop_if_goal = true;
        options.iw1_precheck_add_effect_novelty = true;
        options.iw1_atom_first_mode = atom_first_mode;
        options.iw1_atom_first_ratio = 2.0;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(brfs.get_search_context(), result);
        }
        return make_brfs_run_trace(result, *event_handler);
    };

    auto baseline_run = [](BeamNoveltyMode beam_novelty_mode)
    {
        auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "driverlog/domain.pddl"),
                                        fs::path(std::string(DATA_DIR) + "driverlog/test_problem.pddl"));
        auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

        auto options = brfs::Options();
        options.event_handler = event_handler;
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
        options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(brfs.get_problem(), 1, true, false, true);
        options.beam_width = 64;
        options.beam_novelty_mode = beam_novelty_mode;
        options.parallel_beam_num_threads = 1;
        options.stop_if_goal = true;

        const auto result = brfs::find_solution(brfs.get_search_context(), options);
        if (result.plan.has_value())
        {
            expect_plan_reaches_goal(brfs.get_search_context(), result);
        }
        return make_brfs_run_trace(result, *event_handler);
    };

    for (const auto beam_novelty_mode : { BeamNoveltyMode::ALL_TESTED, BeamNoveltyMode::SURVIVORS_ONLY })
    {
        SCOPED_TRACE(beam_novelty_mode == BeamNoveltyMode::ALL_TESTED ? "all_tested" : "survivors_only");
        const auto baseline_trace = baseline_run(beam_novelty_mode);
        for (const auto atom_first_mode : { false, true })
        {
            SCOPED_TRACE(atom_first_mode ? "atom_first" : "action_first");
            for (const auto parallel_threads : { 1u, 2u, 4u })
            {
                SCOPED_TRACE(parallel_threads);
                expect_brfs_run_traces_match(run(beam_novelty_mode, parallel_threads, atom_first_mode), baseline_trace);
            }
        }
    }
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamDuplicatePruningCoversDeliveryStateSpaceTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "delivery/domain.pddl"),
                                    fs::path(std::string(DATA_DIR) + "delivery/test_problem.pddl"));
    auto event_handler = std::make_shared<RecordingBrFSEventHandler>(brfs.get_problem());

    const auto state_space_result = datasets::StateSpaceImpl::create(brfs.get_search_context());
    ASSERT_TRUE(state_space_result.has_value());
    const auto& state_space = state_space_result->first;
    auto state_space_sampler = datasets::StateSpaceSamplerImpl(state_space);

    auto expected_state_indices = std::vector<Index> {};
    expected_state_indices.reserve(state_space->get_graph().get_num_vertices());
    for (const auto& vertex : state_space->get_graph().get_vertices())
    {
        expected_state_indices.push_back(graphs::get_state(vertex).get_index());
    }
    std::sort(expected_state_indices.begin(), expected_state_indices.end());

    auto options = brfs::Options();
    options.event_handler = event_handler;
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
    options.beam_width = static_cast<uint32_t>(state_space->get_graph().get_num_vertices());
    options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
    options.parallel_beam_num_threads = 4;
    options.stop_if_goal = false;

    const auto result = brfs::find_solution(brfs.get_search_context(), options);
    EXPECT_EQ(result.status, SearchStatus::EXHAUSTED);

    auto actual_state_indices = get_state_indices(event_handler->get_expanded_states());
    std::sort(actual_state_indices.begin(), actual_state_indices.end());
    EXPECT_EQ(actual_state_indices, expected_state_indices);

    auto& applicable_action_generator = *brfs.get_search_context()->get_applicable_action_generator();
    auto& state_repository = *brfs.get_search_context()->get_state_repository();
    for (const auto& vertex : state_space->get_graph().get_vertices())
    {
        const auto state = graphs::get_state(vertex);

        auto expected_transitions = std::vector<std::pair<Index, Index>> {};
        for (const auto& [action, successor_state] : state_space_sampler.get_forward_transitions(state))
        {
            expected_transitions.emplace_back(action->get_index(), successor_state.get_index());
        }
        std::sort(expected_transitions.begin(), expected_transitions.end());

        auto actual_transitions = std::vector<std::pair<Index, Index>> {};
        for (const auto& action : applicable_action_generator.create_applicable_action_generator(state))
        {
            const auto [successor_state, successor_metric_value] = state_repository.get_or_create_successor_state(state, action, 0.0);
            [[maybe_unused]] const auto ignored_successor_metric_value = successor_metric_value;
            actual_transitions.emplace_back(action->get_index(), successor_state.get_index());
        }
        std::sort(actual_transitions.begin(), actual_transitions.end());

        EXPECT_EQ(actual_transitions, expected_transitions);
    }
}

/**
 * Hiking
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedHikingTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "hiking/domain.pddl"), fs::path(std::string(DATA_DIR) + "hiking/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 145);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 24);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedHikingTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "hiking/domain.pddl"), fs::path(std::string(DATA_DIR) + "hiking/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 145);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 24);
}

/**
 * Logistics
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedLogisticsTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "logistics/domain.pddl"), fs::path(std::string(DATA_DIR) + "logistics/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 43);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 8);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedLogisticsTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "logistics/domain.pddl"), fs::path(std::string(DATA_DIR) + "logistics/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 43);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 8);
}

/**
 * Miconic
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedMiconicTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "miconic/domain.pddl"), fs::path(std::string(DATA_DIR) + "miconic/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 5);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 26);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 14);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedMiconicTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "miconic/domain.pddl"), fs::path(std::string(DATA_DIR) + "miconic/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 5);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 26);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 14);
}

/**
 * Miconic-fulladl
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedMiconicFullAdlTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "miconic-fulladl/domain.pddl"),
                                    fs::path(std::string(DATA_DIR) + "miconic-fulladl/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 7);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 105);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 41);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedMiconicFullAdlTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "miconic-fulladl/domain.pddl"),
                                  fs::path(std::string(DATA_DIR) + "miconic-fulladl/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 7);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 105);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 41);
}

/**
 * Miconic-simpleadl
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedMiconicSimpleAdlTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "miconic-simpleadl/domain.pddl"),
                                    fs::path(std::string(DATA_DIR) + "miconic-simpleadl/test_problem.pddl"));

    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 8);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 4);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedMiconicSimpleAdlTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "miconic-simpleadl/domain.pddl"),
                                  fs::path(std::string(DATA_DIR) + "miconic-simpleadl/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 8);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 4);
}

/**
 * Philosophers
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedPhilosophersTest)
{
    auto brfs =
        GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "philosophers/domain.pddl"), fs::path(std::string(DATA_DIR) + "philosophers/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 18);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 210);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 125);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedPhilosophersTest)
{
    auto brfs =
        LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "philosophers/domain.pddl"), fs::path(std::string(DATA_DIR) + "philosophers/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 18);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 210);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 125);
}

/**
 * Reward
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedRewardTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "reward/domain.pddl"), fs::path(std::string(DATA_DIR) + "reward/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 12);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 7);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedRewardTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "reward/domain.pddl"), fs::path(std::string(DATA_DIR) + "reward/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 12);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 7);
}

/**
 * Rovers
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedRoversTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "rovers/domain.pddl"), fs::path(std::string(DATA_DIR) + "rovers/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 24);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 10);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedRoversTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "rovers/domain.pddl"), fs::path(std::string(DATA_DIR) + "rovers/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 24);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 10);
}

/**
 * Satellite
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedSatelliteTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "satellite/domain.pddl"), fs::path(std::string(DATA_DIR) + "satellite/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 7);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 303);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 56);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedSatelliteTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "satellite/domain.pddl"), fs::path(std::string(DATA_DIR) + "satellite/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 7);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 303);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 56);
}

/**
 * Schedule
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedScheduleTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "schedule/domain.pddl"), fs::path(std::string(DATA_DIR) + "schedule/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 2);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 884);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 45);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedScheduleTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "schedule/domain.pddl"), fs::path(std::string(DATA_DIR) + "schedule/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 2);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 884);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 45);
}

/**
 * Spanner
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedSpannerTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "spanner/domain.pddl"), fs::path(std::string(DATA_DIR) + "spanner/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 5);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 5);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedSpannerTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "spanner/domain.pddl"), fs::path(std::string(DATA_DIR) + "spanner/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 4);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 5);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 5);
}

TEST(MimirTests, SearchAlgorithmsBrFSIW1IncrementalPositiveTriggerTest)
{
    auto planner = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "iw1_incremental/domain.pddl"),
                                     fs::path(std::string(DATA_DIR) + "iw1_incremental/positive_problem.pddl"));
    const auto event_handler = brfs::DefaultEventHandlerImpl::create(planner.get_problem());
    const auto options = make_classical_iw1_brfs_options(planner.get_problem(), event_handler);

    const auto result = brfs::find_solution(planner.get_search_context(), options);

    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(result.plan.has_value());
    EXPECT_EQ(get_plan_action_signatures(*result.plan), std::vector<std::string>({ "enable(a)", "use-enabled(a)" }));

    const auto& incremental_statistics = event_handler->get_statistics().get_iw1_incremental_first_applicability_statistics();
    EXPECT_GE(incremental_statistics.get_num_non_root_states_using_incremental_path(), 1);
    EXPECT_GT(incremental_statistics.get_num_partial_seeds_created(), 0);
    EXPECT_GT(incremental_statistics.get_num_ground_actions_returned_by_partial_completion(), 0);
}

TEST(MimirTests, SearchAlgorithmsBrFSIW1IncrementalAddEffectPrecheckPlainMatchesBaselineTest)
{
    auto planner = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "iw1_incremental/domain.pddl"),
                                     fs::path(std::string(DATA_DIR) + "iw1_incremental/positive_problem.pddl"));

    const auto combined_event_handler = brfs::DefaultEventHandlerImpl::create(planner.get_problem());
    auto combined_options = make_classical_iw1_brfs_options(planner.get_problem(), combined_event_handler, true, true);
    combined_options.iw1_precheck_add_effect_novelty = true;
    const auto combined_result = brfs::find_solution(planner.get_search_context(), combined_options);

    EXPECT_EQ(combined_result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(combined_result.plan.has_value());
    EXPECT_EQ(get_plan_action_signatures(*combined_result.plan), std::vector<std::string>({ "enable(a)", "use-enabled(a)" }));

    const auto& incremental_statistics = combined_event_handler->get_statistics().get_iw1_incremental_first_applicability_statistics();
    EXPECT_GE(incremental_statistics.get_num_non_root_states_using_incremental_path(), 1);
}

TEST(MimirTests, SearchAlgorithmsBrFSIW1IncrementalNegativeTriggerTest)
{
    auto planner = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "iw1_incremental/domain.pddl"),
                                     fs::path(std::string(DATA_DIR) + "iw1_incremental/negative_problem.pddl"));
    const auto event_handler = brfs::DefaultEventHandlerImpl::create(planner.get_problem());
    const auto options = make_classical_iw1_brfs_options(planner.get_problem(), event_handler);

    const auto result = brfs::find_solution(planner.get_search_context(), options);

    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(result.plan.has_value());
    EXPECT_EQ(get_plan_action_signatures(*result.plan), std::vector<std::string>({ "remove-present(a)", "use-not-present(a)" }));

    const auto& incremental_statistics = event_handler->get_statistics().get_iw1_incremental_first_applicability_statistics();
    EXPECT_EQ(incremental_statistics.get_num_non_root_states_using_incremental_path(), 1);
    EXPECT_GT(incremental_statistics.get_num_partial_seeds_created(), 0);
}

TEST(MimirTests, SearchAlgorithmsBrFSIW1IncrementalRepeatedVariableTriggerTest)
{
    auto planner = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "iw1_incremental/domain.pddl"),
                                     fs::path(std::string(DATA_DIR) + "iw1_incremental/repeated_problem.pddl"));
    const auto event_handler = brfs::DefaultEventHandlerImpl::create(planner.get_problem());
    const auto options = make_classical_iw1_brfs_options(planner.get_problem(), event_handler);

    const auto result = brfs::find_solution(planner.get_search_context(), options);

    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(result.plan.has_value());
    EXPECT_EQ(get_plan_action_signatures(*result.plan), std::vector<std::string>({ "activate-diag(a)", "use-diag(a)" }));
}

TEST(MimirTests, SearchAlgorithmsBrFSIW1IncrementalConstantTriggerTest)
{
    auto planner = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "iw1_incremental/domain.pddl"),
                                     fs::path(std::string(DATA_DIR) + "iw1_incremental/constant_problem.pddl"));
    const auto event_handler = brfs::DefaultEventHandlerImpl::create(planner.get_problem());
    const auto options = make_classical_iw1_brfs_options(planner.get_problem(), event_handler);

    const auto result = brfs::find_solution(planner.get_search_context(), options);

    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(result.plan.has_value());
    EXPECT_EQ(get_plan_action_signatures(*result.plan), std::vector<std::string>({ "open-hub()", "use-hub()" }));
}

TEST(MimirTests, SearchAlgorithmsBrFSIW1IncrementalEverTestedGroundActionsTest)
{
    auto planner = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "iw1_incremental/domain.pddl"),
                                     fs::path(std::string(DATA_DIR) + "iw1_incremental/ever_tested_problem.pddl"));
    const auto event_handler = brfs::DefaultEventHandlerImpl::create(planner.get_problem());
    auto options = make_classical_iw1_brfs_options(planner.get_problem(), event_handler);
    options.stop_if_goal = false;
    options.max_depth = 2;

    const auto result = brfs::find_solution(planner.get_search_context(), options);

    EXPECT_EQ(result.status, SearchStatus::EXHAUSTED);

    const auto& incremental_statistics = event_handler->get_statistics().get_iw1_incremental_first_applicability_statistics();
    EXPECT_GE(incremental_statistics.get_num_non_root_states_using_incremental_path(), 2);
    EXPECT_GE(incremental_statistics.get_num_already_tested_actions_skipped(), 1);
    EXPECT_GE(incremental_statistics.get_num_non_root_states_with_zero_returned_actions(), 1);
}

TEST(MimirTests, SearchAlgorithmsBrFSIW1IncrementalFilteredCandidatesDoNotEnterEverTestedTest)
{
    auto planner = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "iw1_incremental/domain.pddl"),
                                     fs::path(std::string(DATA_DIR) + "iw1_incremental/precheck_filtered_problem.pddl"));
    const auto event_handler = brfs::DefaultEventHandlerImpl::create(planner.get_problem());
    auto options = make_classical_iw1_brfs_options(planner.get_problem(), event_handler, true, false);
    options.iw1_precheck_add_effect_novelty = true;
    options.stop_if_goal = false;
    options.max_depth = 2;

    const auto result = brfs::find_solution(planner.get_search_context(), options);

    EXPECT_EQ(result.status, SearchStatus::EXHAUSTED);

    const auto& incremental_statistics = event_handler->get_statistics().get_iw1_incremental_first_applicability_statistics();
    EXPECT_GE(incremental_statistics.get_num_non_root_states_using_incremental_path(), 2);
    EXPECT_GT(incremental_statistics.get_num_partial_seeds_created(), 0);
    EXPECT_EQ(incremental_statistics.get_num_already_tested_actions_skipped(), 0);
}

TEST(MimirTests, SearchAlgorithmsBrFSIW1IncrementalBeamAllTestedAddEffectPrecheckMatchesBaselineTest)
{
    auto planner = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "iw1_incremental/domain.pddl"),
                                     fs::path(std::string(DATA_DIR) + "iw1_incremental/positive_problem.pddl"));

    const auto combined_event_handler = brfs::DefaultEventHandlerImpl::create(planner.get_problem());
    auto combined_options = make_classical_iw1_beam_options(planner.get_problem(), combined_event_handler, true, true, true);
    const auto combined_result = brfs::find_solution(planner.get_search_context(), combined_options);

    EXPECT_EQ(combined_result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(combined_result.plan.has_value());
    EXPECT_EQ(get_plan_action_signatures(*combined_result.plan), std::vector<std::string>({ "enable(a)", "use-enabled(a)" }));

    const auto& incremental_statistics = combined_event_handler->get_statistics().get_iw1_incremental_first_applicability_statistics();
    EXPECT_GE(incremental_statistics.get_num_root_actions_fully_enumerated(), 1);
    EXPECT_GE(incremental_statistics.get_num_non_root_states_using_incremental_path(), 1);
}

TEST(MimirTests, SearchAlgorithmsBrFSProjectiveIW1IncrementalBeamAllTestedAddEffectPrecheckMatchesBaselineTest)
{
    auto planner = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "iw1_incremental/domain.pddl"),
                                     fs::path(std::string(DATA_DIR) + "iw1_incremental/positive_problem.pddl"));

    const auto combined_event_handler = brfs::DefaultEventHandlerImpl::create(planner.get_problem());
    auto combined_options = make_projective_iw1_beam_options(planner.get_problem(), combined_event_handler, true, true, true);
    const auto combined_result = brfs::find_solution(planner.get_search_context(), combined_options);

    EXPECT_EQ(combined_result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(combined_result.plan.has_value());
    EXPECT_EQ(get_plan_action_signatures(*combined_result.plan), std::vector<std::string>({ "enable(a)", "use-enabled(a)" }));

    const auto& incremental_statistics = combined_event_handler->get_statistics().get_iw1_incremental_first_applicability_statistics();
    EXPECT_GE(incremental_statistics.get_num_root_actions_fully_enumerated(), 1);
    EXPECT_GE(incremental_statistics.get_num_non_root_states_using_incremental_path(), 1);
}

TEST(MimirTests, SearchAlgorithmsBrFSIW1IncrementalBeamSurvivorsOnlyRejectedTest)
{
    auto planner = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "iw1_incremental/domain.pddl"),
                                     fs::path(std::string(DATA_DIR) + "iw1_incremental/positive_problem.pddl"));
    const auto event_handler = brfs::DefaultEventHandlerImpl::create(planner.get_problem());
    auto options = make_classical_iw1_beam_options(planner.get_problem(), event_handler, true, false, true);
    options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;

    EXPECT_THROW(brfs::find_solution(planner.get_search_context(), options), std::invalid_argument);
}

TEST(MimirTests, SearchAlgorithmsBrFSProjectiveIW1IncrementalCrosscheckTest)
{
    auto planner = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "iw1_incremental/domain.pddl"),
                                     fs::path(std::string(DATA_DIR) + "iw1_incremental/positive_problem.pddl"));
    const auto event_handler = brfs::DefaultEventHandlerImpl::create(planner.get_problem());
    const auto options = make_projective_iw1_brfs_options(planner.get_problem(), event_handler);

    const auto result = brfs::find_solution(planner.get_search_context(), options);

    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(result.plan.has_value());
    EXPECT_EQ(get_plan_action_signatures(*result.plan), std::vector<std::string>({ "enable(a)", "use-enabled(a)" }));
}

TEST(MimirTests, SearchAlgorithmsBrFSIW1IncrementalSpannerRegressionTest)
{
    auto planner = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "spanner/domain.pddl"),
                                     fs::path(std::string(DATA_DIR) + "spanner/iw1_incremental_regression.pddl"));
    const auto event_handler = brfs::DefaultEventHandlerImpl::create(planner.get_problem());
    auto options = make_classical_iw1_brfs_options(planner.get_problem(), event_handler, true, false);
    options.stop_if_goal = false;
    options.max_depth = 2;

    const auto result = brfs::find_solution(planner.get_search_context(), options);

    EXPECT_EQ(result.status, SearchStatus::EXHAUSTED);

    const auto& incremental_statistics = event_handler->get_statistics().get_iw1_incremental_first_applicability_statistics();
    EXPECT_EQ(incremental_statistics.get_num_root_actions_fully_enumerated(), 5);
    EXPECT_EQ(incremental_statistics.get_num_non_root_states_using_incremental_path(), 1);
    EXPECT_EQ(incremental_statistics.get_num_changed_atoms_processed(), 3);
    EXPECT_EQ(incremental_statistics.get_num_partial_seeds_created(), 0);
    EXPECT_EQ(incremental_statistics.get_num_non_root_states_with_zero_returned_actions(), 1);
}

TEST(MimirTests, SearchAlgorithmsBrFSIW1IncrementalBeamAllTestedSpannerRegressionTest)
{
    auto planner = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "spanner/domain.pddl"),
                                     fs::path(std::string(DATA_DIR) + "spanner/iw1_incremental_regression.pddl"));
    const auto event_handler = brfs::DefaultEventHandlerImpl::create(planner.get_problem());
    auto options = make_classical_iw1_beam_options(planner.get_problem(), event_handler, true, false, true);
    options.stop_if_goal = false;
    options.max_depth = 2;

    const auto result = brfs::find_solution(planner.get_search_context(), options);

    EXPECT_EQ(result.status, SearchStatus::EXHAUSTED);

    const auto& incremental_statistics = event_handler->get_statistics().get_iw1_incremental_first_applicability_statistics();
    EXPECT_EQ(incremental_statistics.get_num_root_actions_fully_enumerated(), 5);
    EXPECT_EQ(incremental_statistics.get_num_non_root_states_using_incremental_path(), 1);
    EXPECT_EQ(incremental_statistics.get_num_changed_atoms_processed(), 3);
    EXPECT_EQ(incremental_statistics.get_num_non_root_states_with_zero_returned_actions(), 1);

}

/**
 * Transport
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedTransportTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "transport/domain.pddl"), fs::path(std::string(DATA_DIR) + "transport/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 5);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 384);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 85);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedTransportTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "transport/domain.pddl"), fs::path(std::string(DATA_DIR) + "transport/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 5);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 384);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 85);
}

/**
 * Visitall
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedVisitallTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "visitall/domain.pddl"), fs::path(std::string(DATA_DIR) + "visitall/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 8);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 77);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 41);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedVisitallTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "visitall/domain.pddl"), fs::path(std::string(DATA_DIR) + "visitall/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 8);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 77);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 41);
}

/**
 * Woodworking
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedWoodworkingTest)
{
    auto brfs =
        GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "woodworking/domain.pddl"), fs::path(std::string(DATA_DIR) + "woodworking/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 2);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 10);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 3);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedWoodworkingTest)
{
    auto brfs =
        LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "woodworking/domain.pddl"), fs::path(std::string(DATA_DIR) + "woodworking/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 2);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 10);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 3);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Numeric planning
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Fo-counters
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedFoCountersTest)
{
    auto brfs =
        GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "fo-counters/domain.pddl"), fs::path(std::string(DATA_DIR) + "fo-counters/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 5);
    EXPECT_EQ(result.plan.value().get_cost(), 5);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 1071);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 113);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedFoCountersTest)
{
    auto brfs =
        LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "fo-counters/domain.pddl"), fs::path(std::string(DATA_DIR) + "fo-counters/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 5);
    EXPECT_EQ(result.plan.value().get_cost(), 5);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 1071);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 113);
}

/**
 * Tpp
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedTppNumericTest)
{
    auto brfs =
        GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "tpp/numeric/domain.pddl"), fs::path(std::string(DATA_DIR) + "tpp/numeric/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 7);
    EXPECT_EQ(result.plan.value().get_cost(), 2012.93);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 2155);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 367);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedTppNumericTest)
{
    auto brfs =
        LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "tpp/numeric/domain.pddl"), fs::path(std::string(DATA_DIR) + "tpp/numeric/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 7);
    EXPECT_EQ(result.plan.value().get_cost(), 2012.93);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 2155);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 367);
}

/**
 * Zenotravel
 */

TEST(MimirTests, SearchAlgorithmsBrFSGroundedZenotravelNumericTest)
{
    auto brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "zenotravel/numeric/domain.pddl"),
                                    fs::path(std::string(DATA_DIR) + "zenotravel/numeric/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 9);
    EXPECT_EQ(result.plan.value().get_cost(), 5952);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 5775);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 1084);
}

TEST(MimirTests, SearchAlgorithmsBrFSLiftedZenotravelNumericTest)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "zenotravel/numeric/domain.pddl"),
                                  fs::path(std::string(DATA_DIR) + "zenotravel/numeric/test_problem.pddl"));
    const auto result = brfs.find_solution();
    EXPECT_EQ(result.status, SearchStatus::SOLVED);
    EXPECT_EQ(result.plan.value().get_actions().size(), 9);
    EXPECT_EQ(result.plan.value().get_cost(), 5952);

    const auto& brfs_statistics = brfs.get_algorithm_statistics();

    EXPECT_EQ(brfs_statistics.get_num_generated_until_g_value().back(), 5775);
    EXPECT_EQ(brfs_statistics.get_num_expanded_until_g_value().back(), 1084);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Landmark-informed transition ordering
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static SearchContext make_grounded_context(const Problem& problem)
{
    return SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
}

static landmarks::FactLandmarkGraph make_landmarks(const Problem& problem,
                                                   landmarks::FactLandmarkGeneratorOptions options = landmarks::FactLandmarkGeneratorOptions())
{
    const auto grounder = LiftedGrounder(problem);
    return landmarks::ApproximateFactLandmarkGenerator::create(grounder, options);
}

static std::set<std::string> get_landmark_predicate_names(const landmarks::FactLandmarkGraph& landmark_graph)
{
    auto names = std::set<std::string> {};

    for (const auto atom : landmark_graph->get_landmark_atoms())
    {
        names.insert(atom->get_predicate()->get_name());
    }

    return names;
}

static std::vector<formalism::GroundAction> get_applicable_actions(const SearchContext& context, const State& state)
{
    auto actions = std::vector<formalism::GroundAction> {};

    for (const auto& action : context->get_applicable_action_generator()->create_applicable_action_generator(state))
    {
        actions.push_back(action);
    }

    return actions;
}

static formalism::GroundAction find_action_by_name(const std::vector<formalism::GroundAction>& actions, const std::string& name)
{
    const auto it = std::find_if(actions.begin(), actions.end(), [&](formalism::GroundAction action) { return action->get_action()->get_name() == name; });
    return (it == actions.end()) ? nullptr : *it;
}

static std::vector<std::string> get_transition_action_names(const std::vector<RecordedTransition>& transitions)
{
    auto names = std::vector<std::string> {};
    names.reserve(transitions.size());

    for (const auto& transition : transitions)
    {
        names.push_back(transition.action->get_action()->get_name());
    }

    return names;
}

static LandmarkTransitionScore
make_landmark_score(uint32_t num_new_landmarks, uint32_t num_new_unique_landmarks, bool unique_achiever_action, uint32_t num_deleted_achieved_landmarks)
{
    return LandmarkTransitionScore { num_new_landmarks,
                                     num_new_unique_landmarks,
                                     unique_achiever_action,
                                     (num_deleted_achieved_landmarks > 0),
                                     num_deleted_achieved_landmarks };
}

/// Acceptance criterion #8: the 2-argument `find_solution(context, options)` overload -- which every
/// pre-existing caller uses and which now routes through `find_solution_impl<QueuedTransitionOrderingStrategy>`
/// -- must produce exactly the pre-change status/plan/statistics. The expected numbers below are the
/// ones asserted by the pre-existing gripper/delivery tests in this file.
TEST(MimirTests, SearchAlgorithmsBrFSDefaultOverloadBehaviorParityTest)
{
    for (const auto& [domain_name, expected_plan_length, expected_generated, expected_expanded] :
         std::vector<std::tuple<std::string, size_t, uint64_t, uint64_t>> { { "gripper", 3u, 44u, 12u }, { "delivery", 4u, 18u, 7u } })
    {
        auto grounded_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + domain_name + "/domain.pddl"),
                                                 fs::path(std::string(DATA_DIR) + domain_name + "/test_problem.pddl"));
        const auto grounded_result = grounded_brfs.find_solution();

        EXPECT_EQ(grounded_result.status, SearchStatus::SOLVED) << domain_name;
        ASSERT_TRUE(grounded_result.plan.has_value()) << domain_name;
        EXPECT_EQ(grounded_result.plan->get_actions().size(), expected_plan_length) << domain_name;
        EXPECT_EQ(grounded_brfs.get_algorithm_statistics().get_num_generated_until_g_value().back(), expected_generated) << domain_name;
        EXPECT_EQ(grounded_brfs.get_algorithm_statistics().get_num_expanded_until_g_value().back(), expected_expanded) << domain_name;
        expect_plan_reaches_goal(grounded_brfs.get_search_context(), grounded_result);

        auto lifted_brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + domain_name + "/domain.pddl"),
                                             fs::path(std::string(DATA_DIR) + domain_name + "/test_problem.pddl"));
        const auto lifted_result = lifted_brfs.find_solution();

        EXPECT_EQ(lifted_result.status, SearchStatus::SOLVED) << domain_name;
        ASSERT_TRUE(lifted_result.plan.has_value()) << domain_name;
        EXPECT_EQ(lifted_result.plan->get_actions().size(), expected_plan_length) << domain_name;
        EXPECT_EQ(lifted_brfs.get_algorithm_statistics().get_num_generated_until_g_value().back(), expected_generated) << domain_name;
        EXPECT_EQ(lifted_brfs.get_algorithm_statistics().get_num_expanded_until_g_value().back(), expected_expanded) << domain_name;
        expect_plan_reaches_goal(lifted_brfs.get_search_context(), lifted_result);

        // Deliberately NOT asserting that the grounded and lifted plans use the identical action
        // sequence. That is not an invariant of the library: the two applicable-action generators
        // enumerate in different orders, so among equal-cost alternatives either may be selected
        // first. gripper is exactly such a case -- ball2 can be carried by `left` or `right`, giving
        // two equally optimal 3-step plans. Each plan is instead validated on its own terms above
        // (status, length, generated/expanded counts, and that executing it actually reaches the
        // goal), which is what "default overload behavior is unchanged" genuinely requires.
    }
}

/// The default queued path must also stay bit-identical at the level of the full expansion/generation
/// trace, not just the summary statistics.
TEST(MimirTests, SearchAlgorithmsBrFSDefaultOverloadExhaustiveTraceParityTest)
{
    auto first_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto first_event_handler = std::make_shared<RecordingBrFSEventHandler>(first_brfs.get_problem());

    auto first_options = brfs::Options();
    first_options.event_handler = first_event_handler;
    first_options.stop_if_goal = false;

    const auto first_result = brfs::find_solution(first_brfs.get_search_context(), first_options);
    ASSERT_EQ(first_result.status, SearchStatus::EXHAUSTED);

    auto second_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto second_event_handler = std::make_shared<RecordingBrFSEventHandler>(second_brfs.get_problem());

    auto second_options = brfs::Options();
    second_options.event_handler = second_event_handler;
    second_options.stop_if_goal = false;

    const auto second_result = brfs::find_solution(second_brfs.get_search_context(), second_options);

    expect_brfs_run_traces_match(make_brfs_run_trace(first_result, *first_event_handler), make_brfs_run_trace(second_result, *second_event_handler));

    // The same context, run with the default stop_if_goal, must still hit the documented pre-change
    // gripper baseline of 44 generated / 12 expanded.
    auto stopping_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"),
                                             fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    const auto stopping_result = stopping_brfs.find_solution();
    ASSERT_EQ(stopping_result.status, SearchStatus::SOLVED);
    EXPECT_EQ(stopping_brfs.get_algorithm_statistics().get_num_generated_until_g_value().back(), 44u);
    EXPECT_EQ(stopping_brfs.get_algorithm_statistics().get_num_expanded_until_g_value().back(), 12u);
}

TEST(MimirTests, SearchAlgorithmsBrFSLandmarkTransitionScoreFieldsMatchTransitionSemanticsTest)
{
    const auto problem = ProblemImpl::create(fs::path(std::string(DATA_DIR) + "landmark_transition_ordering/domain.pddl"),
                                             fs::path(std::string(DATA_DIR) + "landmark_transition_ordering/test_problem.pddl"));
    const auto landmark_graph = make_landmarks(problem);

    // The landmark set is exactly {(g), (w), (p)}: (g) is the goal, and (w)/(p) are the positive
    // fluent preconditions of its unique achiever `finish`. (q) is deliberately NOT a landmark, and
    // (r)/(enabled) are static predicates (they occur in no effect) so they cannot be fluent landmarks.
    EXPECT_EQ(get_landmark_predicate_names(landmark_graph), (std::set<std::string> { "g", "p", "w" }));

    const auto context = make_grounded_context(problem);
    auto& state_repository = *context->get_state_repository();
    const auto [initial_state, initial_metric_value] = state_repository.get_or_create_initial_state();

    const auto actions = get_applicable_actions(context, initial_state);
    ASSERT_EQ(actions.size(), 2u);

    const auto del_landmark = find_action_by_name(actions, "del-landmark");
    const auto del_nonlandmark = find_action_by_name(actions, "del-nonlandmark");
    ASSERT_NE(del_landmark, nullptr);
    ASSERT_NE(del_nonlandmark, nullptr);

    // `del-landmark` is enumerated first, which is what makes the default queued order pick it.
    EXPECT_EQ(actions.front(), del_landmark);

    const auto del_landmark_successor = state_repository.get_or_create_successor_state(initial_state, del_landmark, initial_metric_value).first;
    const auto del_nonlandmark_successor = state_repository.get_or_create_successor_state(initial_state, del_nonlandmark, initial_metric_value).first;
    EXPECT_NE(del_landmark_successor.get_index(), del_nonlandmark_successor.get_index());

    const auto ordering = LandmarkTransitionOrderingStrategy(landmark_graph);

    const auto del_landmark_score = ordering.score(initial_state, del_landmark, del_landmark_successor, DiscreteCost(1));
    EXPECT_EQ(del_landmark_score.num_new_landmarks, 1u);  // adds the landmark (w)
    EXPECT_EQ(del_landmark_score.num_new_unique_landmarks, 0u);  // (w) has two achievers
    EXPECT_FALSE(del_landmark_score.unique_achiever_action);
    EXPECT_TRUE(del_landmark_score.deletes_achieved_landmark);
    EXPECT_EQ(del_landmark_score.num_deleted_achieved_landmarks, 1u);  // deletes the landmark (p)

    const auto del_nonlandmark_score = ordering.score(initial_state, del_nonlandmark, del_nonlandmark_successor, DiscreteCost(1));
    EXPECT_EQ(del_nonlandmark_score.num_new_landmarks, 1u);
    EXPECT_EQ(del_nonlandmark_score.num_new_unique_landmarks, 0u);
    EXPECT_FALSE(del_nonlandmark_score.unique_achiever_action);
    EXPECT_FALSE(del_nonlandmark_score.deletes_achieved_landmark);
    EXPECT_EQ(del_nonlandmark_score.num_deleted_achieved_landmarks, 0u);  // deletes the non-landmark (q)

    // Only key 4 separates them, and fewer deletions must win.
    EXPECT_TRUE(ordering.prefer(del_nonlandmark_score, del_landmark_score));
    EXPECT_FALSE(ordering.prefer(del_landmark_score, del_nonlandmark_score));

    // `finish` is the unique achiever of the goal landmark (g), so it is a unique landmark achiever.
    const auto goal_landmarks = landmark_graph->get_landmark_atoms();
    const auto goal_atom_it =
        std::find_if(goal_landmarks.begin(), goal_landmarks.end(), [](GroundAtom<FluentTag> atom) { return atom->get_predicate()->get_name() == "g"; });
    ASSERT_NE(goal_atom_it, goal_landmarks.end());

    const auto finish_action = landmark_graph->get_unique_achiever((*goal_atom_it)->get_index());
    ASSERT_TRUE(finish_action.has_value());
    EXPECT_EQ((*finish_action)->get_action()->get_name(), "finish");
    EXPECT_TRUE(landmark_graph->is_unique_landmark_achiever(*finish_action));
}

/// Direct, search-independent assertions on the 4 gated comparison keys of `prefer_impl`, including
/// the fact that key 4 sorts SMALLER-is-better while keys 1-3 sort LARGER-is-better.
TEST(MimirTests, SearchAlgorithmsBrFSLandmarkTransitionPreferenceKeyOrderTest)
{
    const auto problem = ProblemImpl::create(fs::path(std::string(DATA_DIR) + "blocks_4/domain.pddl"),
                                             fs::path(std::string(DATA_DIR) + "blocks_4/test_problem.pddl"));
    const auto ordering = LandmarkTransitionOrderingStrategy(make_landmarks(problem));

    // Key 1: more newly achieved landmarks wins, and dominates every later key.
    EXPECT_TRUE(ordering.prefer(make_landmark_score(2, 0, false, 0), make_landmark_score(1, 0, false, 0)));
    EXPECT_FALSE(ordering.prefer(make_landmark_score(1, 0, false, 0), make_landmark_score(2, 0, false, 0)));
    EXPECT_TRUE(ordering.prefer(make_landmark_score(2, 0, false, 9), make_landmark_score(1, 7, true, 0)));

    // Key 2: more uniquely achieved landmarks wins once key 1 ties, and dominates keys 3-4.
    EXPECT_TRUE(ordering.prefer(make_landmark_score(1, 1, false, 9), make_landmark_score(1, 0, true, 0)));
    EXPECT_FALSE(ordering.prefer(make_landmark_score(1, 0, true, 0), make_landmark_score(1, 1, false, 9)));

    // Key 3: a unique-landmark-achiever action wins once keys 1-2 tie ...
    EXPECT_TRUE(ordering.prefer(make_landmark_score(1, 0, true, 0), make_landmark_score(1, 0, false, 0)));
    EXPECT_FALSE(ordering.prefer(make_landmark_score(1, 0, false, 0), make_landmark_score(1, 0, true, 0)));

    // ... and criterion 3 overrides criterion 4: the unique achiever is preferred even though it
    // deletes strictly more already-achieved landmarks than the alternative.
    EXPECT_TRUE(ordering.prefer(make_landmark_score(1, 0, true, 3), make_landmark_score(1, 0, false, 0)));
    EXPECT_FALSE(ordering.prefer(make_landmark_score(1, 0, false, 0), make_landmark_score(1, 0, true, 3)));

    // Key 4: FEWER deleted achieved landmarks wins -- the opposite direction from keys 1-3. A sign
    // flip here flips both of the following assertions.
    EXPECT_TRUE(ordering.prefer(make_landmark_score(1, 0, false, 0), make_landmark_score(1, 0, false, 1)));
    EXPECT_FALSE(ordering.prefer(make_landmark_score(1, 0, false, 1), make_landmark_score(1, 0, false, 0)));
    EXPECT_TRUE(ordering.prefer(make_landmark_score(1, 0, false, 1), make_landmark_score(1, 0, false, 7)));
    EXPECT_FALSE(ordering.prefer(make_landmark_score(1, 0, false, 7), make_landmark_score(1, 0, false, 1)));

    // Fully equal scores must be incomparable in both directions, so that std::stable_sort keeps
    // generation order (the implicit 5th key).
    EXPECT_FALSE(ordering.prefer(make_landmark_score(1, 1, true, 2), make_landmark_score(1, 1, true, 2)));
    EXPECT_FALSE(ordering.prefer(make_landmark_score(0, 0, false, 0), make_landmark_score(0, 0, false, 0)));
}

/// Each `LandmarkTransitionOrderingOptions` flag must SKIP its key (falling through to the next one),
/// never invert it.
TEST(MimirTests, SearchAlgorithmsBrFSLandmarkTransitionPreferenceOptionFlagsTest)
{
    const auto problem = ProblemImpl::create(fs::path(std::string(DATA_DIR) + "blocks_4/domain.pddl"),
                                             fs::path(std::string(DATA_DIR) + "blocks_4/test_problem.pddl"));
    const auto landmark_graph = make_landmarks(problem);

    const auto make_ordering = [&](bool new_landmarks, bool unique_achievers, bool landmark_actions, bool fewer_deletions)
    {
        auto options = LandmarkTransitionOrderingOptions();
        options.prefer_new_landmarks = new_landmarks;
        options.prefer_unique_achievers = unique_achievers;
        options.prefer_landmark_actions_when_deleting = landmark_actions;
        options.prefer_fewer_deleted_landmarks = fewer_deletions;
        return LandmarkTransitionOrderingStrategy(landmark_graph, options);
    };

    const auto default_ordering = make_ordering(true, true, true, true);

    // prefer_new_landmarks off: key 1 no longer decides, so key 4 does.
    const auto no_new_landmarks = make_ordering(false, true, true, true);
    EXPECT_FALSE(default_ordering.prefer(make_landmark_score(1, 0, false, 0), make_landmark_score(5, 0, false, 1)));
    EXPECT_TRUE(no_new_landmarks.prefer(make_landmark_score(1, 0, false, 0), make_landmark_score(5, 0, false, 1)));

    // prefer_unique_achievers off: key 2 no longer decides, so key 3 does.
    const auto no_unique_achievers = make_ordering(true, false, true, true);
    EXPECT_FALSE(default_ordering.prefer(make_landmark_score(1, 0, true, 0), make_landmark_score(1, 4, false, 0)));
    EXPECT_TRUE(no_unique_achievers.prefer(make_landmark_score(1, 0, true, 0), make_landmark_score(1, 4, false, 0)));

    // prefer_landmark_actions_when_deleting off: key 3 no longer decides, so key 4 does and the
    // criterion-3-overrides-criterion-4 behavior disappears.
    const auto no_landmark_actions = make_ordering(true, true, false, true);
    EXPECT_TRUE(default_ordering.prefer(make_landmark_score(1, 0, true, 3), make_landmark_score(1, 0, false, 0)));
    EXPECT_FALSE(no_landmark_actions.prefer(make_landmark_score(1, 0, true, 3), make_landmark_score(1, 0, false, 0)));
    EXPECT_TRUE(no_landmark_actions.prefer(make_landmark_score(1, 0, false, 0), make_landmark_score(1, 0, true, 3)));

    // prefer_fewer_deleted_landmarks off: deletions stop mattering entirely (skipped, not inverted).
    const auto no_fewer_deletions = make_ordering(true, true, true, false);
    EXPECT_TRUE(default_ordering.prefer(make_landmark_score(1, 0, false, 0), make_landmark_score(1, 0, false, 4)));
    EXPECT_FALSE(no_fewer_deletions.prefer(make_landmark_score(1, 0, false, 0), make_landmark_score(1, 0, false, 4)));
    EXPECT_FALSE(no_fewer_deletions.prefer(make_landmark_score(1, 0, false, 4), make_landmark_score(1, 0, false, 0)));

    // All flags off: every pair is incomparable, i.e. pure generation order.
    const auto all_off = make_ordering(false, false, false, false);
    EXPECT_FALSE(all_off.prefer(make_landmark_score(9, 9, true, 0), make_landmark_score(0, 0, false, 9)));
    EXPECT_FALSE(all_off.prefer(make_landmark_score(0, 0, false, 9), make_landmark_score(9, 9, true, 0)));
}

/// Two depth-1 transitions add the same single fluent atom (w) and therefore compete for the same
/// IW(1) novelty witness: only the first-admitted one is novel. The default queued order admits the
/// first-enumerated action `del-landmark`, which destroys the landmark (p) and makes the goal
/// unreachable; the landmark ordering admits `del-nonlandmark` instead and solves the problem.
TEST(MimirTests, SearchAlgorithmsBrFSLandmarkOrderingWinsSameDepthNoveltyCompetitionTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "landmark_transition_ordering/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "landmark_transition_ordering/test_problem.pddl");

    const auto make_iw1_options = [](const Problem& problem, brfs::EventHandler event_handler)
    {
        const auto& ground_fluent_atom_repository =
            boost::hana::at_key(problem->get_repositories().get_hana_repositories(), boost::hana::type<GroundAtomImpl<FluentTag>> {});

        auto options = brfs::Options();
        options.event_handler = std::move(event_handler);
        options.pruning_strategy = iw::ArityKNoveltyPruningStrategyImpl::create(1, ground_fluent_atom_repository.size());
        return options;
    };

    const auto baseline_problem = ProblemImpl::create(domain_file, problem_file);
    const auto baseline_landmarks = make_landmarks(baseline_problem);
    const auto baseline_context = make_grounded_context(baseline_problem);
    auto baseline_event_handler = std::make_shared<RecordingBrFSEventHandler>(baseline_problem);

    const auto baseline_result = brfs::find_solution(baseline_context, make_iw1_options(baseline_problem, baseline_event_handler));

    EXPECT_EQ(baseline_result.status, SearchStatus::EXHAUSTED);
    EXPECT_FALSE(baseline_result.plan.has_value());
    // `del-landmark` claimed the shared novelty witness (w); `del-nonlandmark` was pruned as non-novel.
    EXPECT_EQ(get_transition_action_names(baseline_event_handler->get_in_search_tree_transitions()), (std::vector<std::string> { "del-landmark" }));
    EXPECT_EQ(baseline_event_handler->get_root_generated_states().size(), 1u);

    const auto landmark_problem = ProblemImpl::create(domain_file, problem_file);
    const auto landmark_graph = make_landmarks(landmark_problem);
    const auto landmark_context = make_grounded_context(landmark_problem);
    auto landmark_event_handler = std::make_shared<RecordingBrFSEventHandler>(landmark_problem);
    const auto ordering = LandmarkTransitionOrderingStrategy(landmark_graph);

    const auto landmark_result = brfs::find_solution(landmark_context, make_iw1_options(landmark_problem, landmark_event_handler), ordering);

    EXPECT_EQ(landmark_result.status, SearchStatus::SOLVED);
    ASSERT_TRUE(landmark_result.plan.has_value());
    EXPECT_EQ(get_plan_action_signatures(*landmark_result.plan), (std::vector<std::string> { "del-nonlandmark()", "finish()" }));
    EXPECT_EQ(get_transition_action_names(landmark_event_handler->get_in_search_tree_transitions()),
              (std::vector<std::string> { "del-nonlandmark", "finish" }));
    expect_plan_reaches_goal(landmark_context, landmark_result);

    // Sanity: both runs saw the same landmark set.
    EXPECT_EQ(get_landmark_predicate_names(baseline_landmarks), get_landmark_predicate_names(landmark_graph));
}

/// Two distinct depth-1 parents reach the SAME depth-2 successor via two different actions with
/// different landmark scores. Only the first-admitted transition is recorded on the successor's
/// search node (the other one loses the duplicate-pruning race), so the recorded parent/action
/// differs between the queued order and the landmark ordering. This is only possible because
/// `is_new_successor` is resolved lazily, in sorted order, at admission time.
TEST(MimirTests, SearchAlgorithmsBrFSLandmarkOrderingWinsCrossParentAdmissionTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "landmark_cross_parent/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "landmark_cross_parent/test_problem.pddl");

    const auto find_admitting_transition = [](const RecordingBrFSEventHandler& event_handler, const std::string& action_name)
    {
        const auto& transitions = event_handler.get_in_search_tree_transitions();
        const auto it = std::find_if(transitions.begin(),
                                     transitions.end(),
                                     [&](const RecordedTransition& transition) { return transition.action->get_action()->get_name() == action_name; });
        return (it == transitions.end()) ? std::nullopt : std::make_optional(*it);
    };

    const auto baseline_problem = ProblemImpl::create(domain_file, problem_file);
    const auto baseline_context = make_grounded_context(baseline_problem);
    auto baseline_event_handler = std::make_shared<RecordingBrFSEventHandler>(baseline_problem);

    auto baseline_options = brfs::Options();
    baseline_options.event_handler = baseline_event_handler;

    const auto baseline_result = brfs::find_solution(baseline_context, baseline_options);
    EXPECT_EQ(baseline_result.status, SearchStatus::EXHAUSTED);

    // Queued order expands P1 before P2, so `join-from-p1` (which deletes the landmark (p)) wins.
    EXPECT_EQ(get_transition_action_names(baseline_event_handler->get_in_search_tree_transitions()),
              (std::vector<std::string> { "to-p1", "to-p2", "join-from-p1" }));
    EXPECT_EQ(get_transition_action_names(baseline_event_handler->get_not_in_search_tree_transitions()),
              (std::vector<std::string> { "join-from-p2" }));

    const auto baseline_to_p1 = find_admitting_transition(*baseline_event_handler, "to-p1");
    const auto baseline_join = find_admitting_transition(*baseline_event_handler, "join-from-p1");
    ASSERT_TRUE(baseline_to_p1.has_value());
    ASSERT_TRUE(baseline_join.has_value());
    EXPECT_EQ(baseline_join->parent_state_index, baseline_to_p1->successor_state_index);

    const auto landmark_problem = ProblemImpl::create(domain_file, problem_file);
    const auto landmark_graph = make_landmarks(landmark_problem);
    const auto landmark_context = make_grounded_context(landmark_problem);
    auto landmark_event_handler = std::make_shared<RecordingBrFSEventHandler>(landmark_problem);

    // (g) is the goal landmark, (t) and (p) are the preconditions of its unique achiever `finish`.
    EXPECT_EQ(get_landmark_predicate_names(landmark_graph), (std::set<std::string> { "g", "p", "t" }));

    auto landmark_options = brfs::Options();
    landmark_options.event_handler = landmark_event_handler;

    const auto landmark_result = brfs::find_solution(landmark_context, landmark_options, LandmarkTransitionOrderingStrategy(landmark_graph));
    EXPECT_EQ(landmark_result.status, SearchStatus::EXHAUSTED);

    // `join-from-p2` deletes no landmark and is therefore admitted first, claiming the shared
    // successor from the OTHER parent than the queued order would have.
    EXPECT_EQ(get_transition_action_names(landmark_event_handler->get_in_search_tree_transitions()),
              (std::vector<std::string> { "to-p1", "to-p2", "join-from-p2" }));
    EXPECT_EQ(get_transition_action_names(landmark_event_handler->get_not_in_search_tree_transitions()),
              (std::vector<std::string> { "join-from-p1" }));

    const auto landmark_to_p2 = find_admitting_transition(*landmark_event_handler, "to-p2");
    const auto landmark_join = find_admitting_transition(*landmark_event_handler, "join-from-p2");
    ASSERT_TRUE(landmark_to_p2.has_value());
    ASSERT_TRUE(landmark_join.has_value());
    // The recorded parent of the shared successor is P2, not P1.
    EXPECT_EQ(landmark_join->parent_state_index, landmark_to_p2->successor_state_index);
    EXPECT_EQ(landmark_join->successor_state_index, baseline_join->successor_state_index);
    EXPECT_NE(landmark_join->parent_state_index, baseline_join->parent_state_index);
}

/// With `include_positive_goal_facts = false` the landmark set is empty, so every transition receives
/// the identical all-zero score and `prefer` is false for every pair. `std::stable_sort` must then
/// preserve raw generation order, which makes the deferred admission pass produce byte-identical
/// behavior to the default queued path.
TEST(MimirTests, SearchAlgorithmsBrFSLandmarkOrderingWithEqualScoresPreservesGenerationOrderTest)
{
    for (const auto& domain_name : std::vector<std::string> { "gripper", "delivery" })
    {
        const auto domain_file = fs::path(std::string(DATA_DIR) + domain_name + "/domain.pddl");
        const auto problem_file = fs::path(std::string(DATA_DIR) + domain_name + "/test_problem.pddl");

        const auto baseline_problem = ProblemImpl::create(domain_file, problem_file);
        const auto baseline_context = make_grounded_context(baseline_problem);
        auto baseline_event_handler = std::make_shared<RecordingBrFSEventHandler>(baseline_problem);

        auto baseline_options = brfs::Options();
        baseline_options.event_handler = baseline_event_handler;
        baseline_options.stop_if_goal = false;

        const auto baseline_result = brfs::find_solution(baseline_context, baseline_options);
        ASSERT_EQ(baseline_result.status, SearchStatus::EXHAUSTED) << domain_name;

        const auto ordered_problem = ProblemImpl::create(domain_file, problem_file);
        auto generator_options = landmarks::FactLandmarkGeneratorOptions();
        generator_options.include_positive_goal_facts = false;
        const auto empty_landmark_graph = make_landmarks(ordered_problem, generator_options);
        ASSERT_TRUE(empty_landmark_graph->get_landmark_atom_indices().empty()) << domain_name;

        const auto ordered_context = make_grounded_context(ordered_problem);
        auto ordered_event_handler = std::make_shared<RecordingBrFSEventHandler>(ordered_problem);

        auto ordered_options = brfs::Options();
        ordered_options.event_handler = ordered_event_handler;
        ordered_options.stop_if_goal = false;

        const auto ordered_result =
            brfs::find_solution(ordered_context, ordered_options, LandmarkTransitionOrderingStrategy(empty_landmark_graph));

        expect_brfs_run_traces_match(make_brfs_run_trace(baseline_result, *baseline_event_handler),
                                     make_brfs_run_trace(ordered_result, *ordered_event_handler));
        EXPECT_EQ(get_transition_action_names(baseline_event_handler->get_in_search_tree_transitions()),
                  get_transition_action_names(ordered_event_handler->get_in_search_tree_transitions()))
            << domain_name;
    }
}

/// Every `brfs::Options` field that the deferred admission loop cannot honor must be rejected with a
/// descriptive `std::invalid_argument` rather than silently ignored.
TEST(MimirTests, SearchAlgorithmsBrFSLandmarkOrderingRejectsUnsupportedOptionsTest)
{
    const auto problem = ProblemImpl::create(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"),
                                             fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    const auto landmark_graph = make_landmarks(problem);
    const auto context = make_grounded_context(problem);
    const auto ordering = LandmarkTransitionOrderingStrategy(landmark_graph);

    const auto& ground_fluent_atom_repository =
        boost::hana::at_key(problem->get_repositories().get_hana_repositories(), boost::hana::type<GroundAtomImpl<FluentTag>> {});

    const auto expect_rejected = [&](const brfs::Options& options, const std::string& expected_fragment)
    {
        try
        {
            brfs::find_solution(context, options, ordering);
            ADD_FAILURE() << "Expected std::invalid_argument mentioning: " << expected_fragment;
        }
        catch (const std::invalid_argument& e)
        {
            EXPECT_NE(std::string(e.what()).find(expected_fragment), std::string::npos) << e.what();
        }
    };

    {
        auto options = brfs::Options();
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
        options.beam_width = 4;
        expect_rejected(options, "BrFS::Options.beam_width is not supported together with a deferred-novelty transition ordering strategy.");
    }

    {
        auto options = brfs::Options();
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
        options.max_next_layer_states = 4;
        expect_rejected(options, "BrFS::Options.max_next_layer_states is not supported together with a deferred-novelty transition ordering strategy.");
    }

    {
        // NOTE: the shared validation block rejects parallel_beam_num_threads > 1 without a
        // beam_width, and with a beam_width the deferred beam_width check fires first. The deferred
        // parallel_beam_num_threads check is therefore currently unreachable; either way the
        // combination is rejected with std::invalid_argument, which is what matters here.
        auto options = brfs::Options();
        options.parallel_beam_num_threads = 2;
        EXPECT_THROW(brfs::find_solution(context, options, ordering), std::invalid_argument);

        auto beam_options = brfs::Options();
        beam_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
        beam_options.beam_width = 4;
        beam_options.parallel_beam_num_threads = 2;
        EXPECT_THROW(brfs::find_solution(context, beam_options, ordering), std::invalid_argument);
    }

    {
        // iw1_incremental_first_applicability additionally requires an applicable-action generator
        // with partial-binding completion support, which the grounded context above does not provide,
        // so this one case needs a lifted KPKC context to reach the deferred-arm rejection.
        const auto lifted_problem = ProblemImpl::create(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"),
                                                        fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
        const auto lifted_context = SearchContextImpl::create(lifted_problem, SearchContextImpl::Options(SearchContextImpl::LiftedOptions()));
        const auto lifted_ordering = LandmarkTransitionOrderingStrategy(make_landmarks(lifted_problem));

        const auto& lifted_ground_fluent_atom_repository =
            boost::hana::at_key(lifted_problem->get_repositories().get_hana_repositories(), boost::hana::type<GroundAtomImpl<FluentTag>> {});

        auto options = brfs::Options();
        options.pruning_strategy = iw::ArityKNoveltyPruningStrategyImpl::create(1, lifted_ground_fluent_atom_repository.size());
        options.iw1_incremental_first_applicability = true;

        try
        {
            brfs::find_solution(lifted_context, options, lifted_ordering);
            ADD_FAILURE() << "Expected std::invalid_argument for iw1_incremental_first_applicability.";
        }
        catch (const std::invalid_argument& e)
        {
            EXPECT_NE(std::string(e.what()).find(
                          "BrFS::Options.iw1_incremental_first_applicability is not supported together with a deferred-novelty transition ordering strategy."),
                      std::string::npos)
                << e.what();
        }
    }

    {
        auto options = brfs::Options();
        options.pruning_strategy = iw::ArityKNoveltyPruningStrategyImpl::create(1, ground_fluent_atom_repository.size());
        options.iw1_atom_first_mode = true;
        expect_rejected(options, "BrFS::Options.iw1_atom_first_mode is not supported together with a deferred-novelty transition ordering strategy.");
    }

    {
        auto options = brfs::Options();
        options.pruning_strategy = iw::ArityKNoveltyPruningStrategyImpl::create(1, ground_fluent_atom_repository.size());
        options.iw1_precheck_add_effect_novelty = true;
        expect_rejected(options,
                        "BrFS::Options.iw1_precheck_add_effect_novelty is not supported together with a deferred-novelty transition ordering strategy.");
    }

    {
        auto options = brfs::Options();
        options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
        expect_rejected(options, "BrFS::Options.layer_ordering_strategy is not supported together with a deferred-novelty transition ordering strategy.");
    }

    // A plain landmark-ordered run with none of the rejected options must still work.
    auto valid_options = brfs::Options();
    EXPECT_NO_THROW(brfs::find_solution(context, valid_options, ordering));
}

}
