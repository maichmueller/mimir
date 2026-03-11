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
#include "mimir/search/algorithms/iw/pruning_strategy.hpp"
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
#include <stdexcept>
#include <string>
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
    LiftedBrFSPlanner(const fs::path& domain_file, const fs::path& problem_file) :
        m_problem(ProblemImpl::create(domain_file, problem_file)),
        m_applicable_action_generator_event_handler(KPKCLiftedApplicableActionGeneratorImpl::DefaultEventHandlerImpl::create()),
        m_applicable_action_generator(KPKCLiftedApplicableActionGeneratorImpl::create(m_problem,
                                                                                      SearchContextImpl::LiftedOptions::KPKCOptions(),
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

class RecordingBrFSEventHandler : public brfs::EventHandlerBase<RecordingBrFSEventHandler>
{
private:
    std::optional<State> m_start_state;
    StateList m_root_generated_states;
    StateList m_expanded_states;

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
        [[maybe_unused]] const auto& ignored_action = action;
        [[maybe_unused]] const auto ignored_action_cost = action_cost;

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
        [[maybe_unused]] const auto& ignored_state = state;
        [[maybe_unused]] const auto& ignored_action = action;
        [[maybe_unused]] const auto ignored_action_cost = action_cost;
        [[maybe_unused]] const auto& ignored_successor_state = successor_state;
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

TEST(MimirTests, SearchAlgorithmsBrFSParallelAllTestedBeamMatchesSerialTest)
{
    auto serial_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto serial_event_handler = std::make_shared<RecordingBrFSEventHandler>(serial_brfs.get_problem());
    auto serial_options = brfs::Options();
    serial_options.event_handler = serial_event_handler;
    serial_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(serial_brfs.get_problem());
    serial_options.pruning_strategy = iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(serial_brfs.get_problem(), false, true, false);
    serial_options.beam_width = 4;
    serial_options.beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
    serial_options.stop_if_goal = false;
    const auto serial_result = brfs::find_solution(serial_brfs.get_search_context(), serial_options);

    auto parallel_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto parallel_event_handler = std::make_shared<RecordingBrFSEventHandler>(parallel_brfs.get_problem());
    auto parallel_options = serial_options;
    parallel_options.event_handler = parallel_event_handler;
    parallel_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(parallel_brfs.get_problem());
    parallel_options.pruning_strategy = iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(parallel_brfs.get_problem(), false, true, false);
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

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamRejectsLiftedContexts)
{
    auto brfs = LiftedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));

    auto options = brfs::Options();
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(brfs.get_problem());
    options.pruning_strategy = iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(brfs.get_problem(), false, true, false);
    options.beam_width = 2;
    options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
    options.parallel_beam_num_threads = 2;
    options.stop_if_goal = false;

    EXPECT_THROW(brfs::find_solution(brfs.get_search_context(), options), std::invalid_argument);
}

TEST(MimirTests, SearchAlgorithmsBrFSParallelBeamThreadsOneUsesSerialPath)
{
    auto serial_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto serial_event_handler = std::make_shared<RecordingBrFSEventHandler>(serial_brfs.get_problem());
    auto serial_options = brfs::Options();
    serial_options.event_handler = serial_event_handler;
    serial_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(serial_brfs.get_problem());
    serial_options.pruning_strategy = iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(serial_brfs.get_problem(), false, true, false);
    serial_options.beam_width = 4;
    serial_options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
    serial_options.stop_if_goal = false;
    const auto serial_result = brfs::find_solution(serial_brfs.get_search_context(), serial_options);

    auto one_thread_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto one_thread_event_handler = std::make_shared<RecordingBrFSEventHandler>(one_thread_brfs.get_problem());
    auto one_thread_options = serial_options;
    one_thread_options.event_handler = one_thread_event_handler;
    one_thread_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(one_thread_brfs.get_problem());
    one_thread_options.pruning_strategy = iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(one_thread_brfs.get_problem(), false, true, false);
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
    serial_options.pruning_strategy = iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(serial_brfs.get_problem(), false, true, true);
    serial_options.beam_width = 4;
    serial_options.beam_novelty_mode = BeamNoveltyMode::SURVIVORS_ONLY;
    serial_options.stop_if_goal = false;
    const auto serial_result = brfs::find_solution(serial_brfs.get_search_context(), serial_options);

    auto parallel_brfs = GroundedBrFSPlanner(fs::path(std::string(DATA_DIR) + "gripper/domain.pddl"), fs::path(std::string(DATA_DIR) + "gripper/test_problem.pddl"));
    auto parallel_event_handler = std::make_shared<RecordingBrFSEventHandler>(parallel_brfs.get_problem());
    auto parallel_options = serial_options;
    parallel_options.event_handler = parallel_event_handler;
    parallel_options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(parallel_brfs.get_problem());
    parallel_options.pruning_strategy = iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(parallel_brfs.get_problem(), false, true, true);
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
        options.pruning_strategy = iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(brfs.get_problem(), false, true, false);
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

    const auto serial_default = run(false, 0, 1);
    const auto serial_seeded = run(true, 7, 1);

    for (const auto parallel_threads : { 2u, 4u, 8u })
    {
        EXPECT_EQ(serial_default, run(false, 0, parallel_threads));
        EXPECT_EQ(serial_seeded, run(true, 7, parallel_threads));
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
        options.pruning_strategy = iw::ProjectiveArityOneNoveltyPruningStrategyImpl::create(brfs.get_problem(),
                                                                                            typed_projection,
                                                                                            keep_depth_one_novel,
                                                                                            keep_goal_nonunary_atoms);
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

}
