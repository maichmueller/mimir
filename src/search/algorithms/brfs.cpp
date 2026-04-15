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

#include "brfs/internal.hpp"
#include "brfs/incremental_iw1.hpp"

#include "mimir/common/timers.hpp"
#include "mimir/formalism/domain.hpp"
#include "mimir/formalism/effects.hpp"
#include "mimir/formalism/formatter.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/search/algorithms/brfs/event_handlers.hpp"
#include "mimir/search/algorithms/brfs/event_handlers/interface.hpp"
#include "mimir/search/algorithms/iw/pruning_strategy.hpp"
#include "mimir/search/algorithms/strategies/goal_strategy.hpp"
#include "mimir/search/algorithms/strategies/layer_ordering_strategy.hpp"
#include "mimir/search/algorithms/strategies/pruning_strategy.hpp"
#include "mimir/search/applicable_action_generators/interface.hpp"
#include "mimir/search/axiom_evaluators/interface.hpp"
#include "mimir/search/axiom_evaluators/lifted/exhaustive.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/search_space.hpp"
#include "mimir/search/state_repository.hpp"

#include <algorithm>
#include <deque>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

using namespace mimir::formalism;

namespace mimir::search::brfs
{
namespace
{
std::string format_ground_action(GroundAction action, Problem problem)
{
    if (!action)
    {
        return "<invalid-action>";
    }

    auto out = std::ostringstream {};
    out << std::tuple<const GroundActionImpl&, const ProblemImpl&, PlanFormatterTag> { *action, *problem, PlanFormatterTag {} };
    return out.str();
}

std::string format_ground_action_list(const std::span<const GroundAction>& actions, Problem problem)
{
    auto out = std::ostringstream {};
    out << "[";
    for (size_t i = 0; i < actions.size(); ++i)
    {
        if (i != 0)
        {
            out << ", ";
        }
        out << format_ground_action(actions[i], problem);
    }
    out << "]";
    return out.str();
}

bool is_empty_conjunctive_condition(ConjunctiveCondition condition)
{
    return condition->get_literals<StaticTag>().empty() && condition->get_literals<FluentTag>().empty() && condition->get_literals<DerivedTag>().empty()
           && condition->get_numeric_constraints().empty();
}

bool supports_iw1_incremental_first_applicability(const ProblemImpl& problem)
{
    if (!problem.get_problem_and_domain_axioms().empty())
    {
        return false;
    }

    for (const auto action : problem.get_domain()->get_actions())
    {
        const auto condition = action->get_conjunctive_condition();
        if (!condition->get_literals<DerivedTag>().empty() || !condition->get_numeric_constraints().empty())
        {
            return false;
        }

        for (const auto& conditional_effect : action->get_conditional_effects())
        {
            if (!is_empty_conjunctive_condition(conditional_effect->get_conjunctive_condition()))
            {
                return false;
            }

            const auto conjunctive_effect = conditional_effect->get_conjunctive_effect();
            if (!conjunctive_effect->get_fluent_numeric_effects().empty() || conjunctive_effect->get_auxiliary_numeric_effect().has_value())
            {
                return false;
            }
        }
    }

    return true;
}

bool supports_iw1_incremental_first_applicability(const PruningStrategy& pruning_strategy)
{
    return std::dynamic_pointer_cast<iw::ProjectiveArityOneNoveltyPruningStrategyImpl>(pruning_strategy) != nullptr
           || pruning_strategy->supports_atom_novelty_query();
}

struct IW1IncrementalStatisticsReporter
{
    EventHandler event_handler;
    IW1IncrementalActionDiscoveryController* controller;

    ~IW1IncrementalStatisticsReporter()
    {
        if (event_handler && controller)
        {
            event_handler->on_finish_iw1_incremental_first_applicability(controller->get_statistics());
        }
    }
};

void run_incremental_precheck_filtered_crosscheck(const SearchContext& context,
                                                 const Options& options,
                                                 const State& start_state,
                                                 const State& state,
                                                 const PruningStrategy& pruning_strategy,
                                                 StateRepositoryImpl& state_repository,
                                                 const IW1IncrementalActionDiscoveryController& iw1_incremental_action_discovery,
                                                 const std::span<const GroundAction>& filtered_incremental_actions)
{
    auto baseline_never_tested = std::vector<GroundAction> {};
    for (const auto action : context->get_applicable_action_generator()->create_applicable_action_generator(state))
    {
        if (!iw1_incremental_action_discovery.has_tested_action(action))
        {
            baseline_never_tested.push_back(action);
        }
    }

    auto debug_precheck = IW1ActionPrecheckController(options, pruning_strategy, context->get_problem(), start_state);
    const auto filtered_baseline_actions = debug_precheck.filter_actions(state, baseline_never_tested, state_repository);

    auto filtered_incremental_indices = std::unordered_set<Index> {};
    for (const auto action : filtered_incremental_actions)
    {
        filtered_incremental_indices.insert(action->get_index());
    }

    auto baseline_indices = std::unordered_set<Index> {};
    auto missing_actions = std::vector<GroundAction> {};
    auto spurious_actions = std::vector<GroundAction> {};

    for (const auto action : filtered_baseline_actions)
    {
        baseline_indices.insert(action->get_index());
        if (!filtered_incremental_indices.contains(action->get_index()))
        {
            missing_actions.push_back(action);
        }
    }

    for (const auto action : filtered_incremental_actions)
    {
        if (!baseline_indices.contains(action->get_index()))
        {
            spurious_actions.push_back(action);
        }
    }

    if (missing_actions.empty() && spurious_actions.empty())
    {
        return;
    }

    auto message = std::ostringstream {};
    message << "IW(1) incremental first-applicability filtered precheck cross-check failed.\n";
    message << "state_id: " << state.get_index() << "\n";
    message << "filtered_incremental_candidates: " << format_ground_action_list(filtered_incremental_actions, context->get_problem()) << "\n";
    message << "filtered_baseline_candidates: " << format_ground_action_list(filtered_baseline_actions, context->get_problem()) << "\n";
    message << "missing_actions: " << format_ground_action_list(missing_actions, context->get_problem()) << "\n";
    message << "spurious_actions: " << format_ground_action_list(spurious_actions, context->get_problem()) << "\n";
    throw std::runtime_error(message.str());
}
}

SearchResult find_solution(const SearchContext& context, const Options& options)
{
    const auto& problem = *context->get_problem();
    auto& applicable_action_generator = *context->get_applicable_action_generator();
    auto& state_repository = *context->get_state_repository();

    const auto [start_state, start_g_value] = (options.start_state) ?
                                                  std::make_pair(options.start_state.value(), compute_state_metric_value(options.start_state.value())) :
                                                  state_repository.get_or_create_initial_state();
    const auto event_handler = (options.event_handler) ? options.event_handler : DefaultEventHandlerImpl::create(context->get_problem());
    const auto goal_strategy = (options.goal_strategy) ? options.goal_strategy : ProblemGoalStrategyImpl::create(context->get_problem());
    const auto pruning_strategy = (options.pruning_strategy) ? options.pruning_strategy : DuplicatePruningStrategyImpl::create();
    const auto layer_ordering_strategy = options.layer_ordering_strategy;
    const auto max_next_layer_states = options.max_next_layer_states;
    const auto use_next_layer_limit = (max_next_layer_states < std::numeric_limits<uint32_t>::max());
    const auto beam_width = options.beam_width;
    const auto use_beam = (beam_width < std::numeric_limits<uint32_t>::max());
    const auto beam_novelty_mode = options.beam_novelty_mode;
    const auto relaxed_survivors_only_beam = options.relaxed_survivors_only_beam;
    const auto parallel_beam_num_threads = options.parallel_beam_num_threads;
    const auto parallel_beam_chunk_size = options.parallel_beam_chunk_size;
    const auto iw1_precheck_add_effect_novelty = options.iw1_precheck_add_effect_novelty;
    const auto iw1_atom_first_mode = options.iw1_atom_first_mode;
    const auto iw1_atom_first_ratio = options.iw1_atom_first_ratio;
    const auto iw1_incremental_first_applicability = options.iw1_incremental_first_applicability;
    const auto iw1_incremental_first_applicability_debug_crosscheck = options.iw1_incremental_first_applicability_debug_crosscheck;
    const auto max_depth = options.max_depth;
    const auto use_max_depth = (max_depth < std::numeric_limits<uint32_t>::max());

    if (use_next_layer_limit && (max_next_layer_states == 0))
    {
        throw std::invalid_argument("BrFS::Options.max_next_layer_states must be positive.");
    }

    if (use_beam && (beam_width == 0))
    {
        throw std::invalid_argument("BrFS::Options.beam_width must be positive.");
    }

    if (use_next_layer_limit && use_beam)
    {
        throw std::invalid_argument("BrFS::Options.max_next_layer_states and BrFS::Options.beam_width are mutually exclusive.");
    }

    if (use_next_layer_limit && !layer_ordering_strategy)
    {
        throw std::invalid_argument("BrFS::Options.max_next_layer_states requires a layer_ordering_strategy.");
    }

    if (use_beam && !layer_ordering_strategy)
    {
        throw std::invalid_argument("BrFS::Options.beam_width requires a layer_ordering_strategy.");
    }

    if (use_beam && !layer_ordering_strategy->supports_eager_scoring())
    {
        throw std::invalid_argument("BrFS::Options.beam_width requires a layer_ordering_strategy with eager scoring support.");
    }

    if (use_beam && !pruning_strategy->supports_beam_novelty_mode(beam_novelty_mode))
    {
        throw std::invalid_argument("The selected pruning_strategy does not support the requested beam novelty mode.");
    }

    if (relaxed_survivors_only_beam)
    {
        if (!use_beam)
        {
            throw std::invalid_argument("BrFS::Options.relaxed_survivors_only_beam requires BrFS::Options.beam_width.");
        }
        if (beam_novelty_mode != BeamNoveltyMode::SURVIVORS_ONLY)
        {
            throw std::invalid_argument("BrFS::Options.relaxed_survivors_only_beam requires BeamNoveltyMode::SURVIVORS_ONLY.");
        }
        if (parallel_beam_num_threads <= 1)
        {
            throw std::invalid_argument("BrFS::Options.relaxed_survivors_only_beam requires BrFS::Options.parallel_beam_num_threads > 1.");
        }
        if (!layer_ordering_strategy->supports_staged_scoring())
        {
            throw std::invalid_argument("BrFS::Options.relaxed_survivors_only_beam requires a layer_ordering_strategy with staged scoring support.");
        }
        if (!pruning_strategy->supports_relaxed_staged_beam_pruning(beam_novelty_mode))
        {
            throw std::invalid_argument("The selected pruning_strategy does not support relaxed staged beam pruning.");
        }
    }

    if (parallel_beam_chunk_size == 0)
    {
        throw std::invalid_argument("BrFS::Options.parallel_beam_chunk_size must be positive.");
    }

    if ((iw1_precheck_add_effect_novelty || iw1_atom_first_mode) && (iw1_atom_first_ratio <= 0.0))
    {
        throw std::invalid_argument("BrFS::Options.iw1_atom_first_ratio must be positive.");
    }

    if (iw1_incremental_first_applicability_debug_crosscheck && !iw1_incremental_first_applicability)
    {
        throw std::invalid_argument(
            "BrFS::Options.iw1_incremental_first_applicability_debug_crosscheck requires BrFS::Options.iw1_incremental_first_applicability.");
    }

    if (iw1_incremental_first_applicability)
    {
        if (use_next_layer_limit)
        {
            throw std::invalid_argument(
                "BrFS::Options.iw1_incremental_first_applicability currently supports only plain BrFS and beam search, but not ordered-layer search.");
        }
        if (use_beam && (beam_novelty_mode != BeamNoveltyMode::ALL_TESTED))
        {
            throw std::invalid_argument(
                "BrFS::Options.iw1_incremental_first_applicability currently supports beam search only with BeamNoveltyMode::ALL_TESTED.");
        }
        if (!use_beam && layer_ordering_strategy)
        {
            throw std::invalid_argument(
                "BrFS::Options.iw1_incremental_first_applicability currently supports plain BrFS without layer ordering, or beam search with BeamNoveltyMode::ALL_TESTED.");
        }
        if (relaxed_survivors_only_beam)
        {
            throw std::invalid_argument(
                "BrFS::Options.iw1_incremental_first_applicability does not support relaxed SURVIVORS_ONLY beam search.");
        }
        if (iw1_atom_first_mode)
        {
            throw std::invalid_argument(
                "BrFS::Options.iw1_incremental_first_applicability cannot currently be combined with IW(1) atom-first mode.");
        }
        if (!supports_iw1_incremental_first_applicability(problem))
        {
            throw std::invalid_argument(
                "BrFS::Options.iw1_incremental_first_applicability requires deterministic STRIPS-style fluent actions without numeric features, derived preconditions, or axioms.");
        }
        if (!supports_iw1_incremental_first_applicability(pruning_strategy))
        {
            throw std::invalid_argument(
                "BrFS::Options.iw1_incremental_first_applicability requires a width-1 novelty pruning strategy.");
        }
        if (!applicable_action_generator.supports_partial_binding_completion())
        {
            throw std::invalid_argument(
                "BrFS::Options.iw1_incremental_first_applicability requires an applicable-action generator with partial-binding completion support.");
        }
    }

    if (parallel_beam_num_threads > 1)
    {
        if (!use_beam)
        {
            throw std::invalid_argument("BrFS::Options.parallel_beam_num_threads requires BrFS::Options.beam_width.");
        }

        if (!state_repository.get_axiom_evaluator()->supports_parallel_staged_successor_evaluation())
        {
            if (std::dynamic_pointer_cast<ExhaustiveLiftedAxiomEvaluatorImpl>(state_repository.get_axiom_evaluator()))
            {
                throw std::invalid_argument(
                    "BrFS::Options.parallel_beam_num_threads does not support lifted exhaustive search contexts. Use grounded or lifted KPKC search contexts.");
            }

            throw std::invalid_argument("BrFS::Options.parallel_beam_num_threads is currently supported only for grounded search contexts and lifted KPKC search contexts.");
        }

        state_repository.get_axiom_evaluator()->prepare_parallel_staged_successor_evaluation();
    }

    auto result = SearchResult();
    auto search_nodes = SearchNodeVector();

    auto& start_search_node = get_or_create_search_node(start_state.get_index(), search_nodes);
    start_search_node.status = SearchNodeStatus::OPEN;
    start_search_node.g_value = 0;
    start_search_node.incoming_action = kInvalidGroundActionIndex;

    event_handler->on_start_search(start_state);

    if (!goal_strategy->test_static_goal())
    {
        event_handler->on_unsolvable();

        result.status = SearchStatus::UNSOLVABLE;
        return result;
    }

    const auto& ground_action_repository = boost::hana::at_key(problem.get_repositories().get_hana_repositories(), boost::hana::type<GroundActionImpl> {});
    const auto& ground_axiom_repository = boost::hana::at_key(problem.get_repositories().get_hana_repositories(), boost::hana::type<GroundAxiomImpl> {});

    if (pruning_strategy->test_prune_initial_state(start_state))
    {
        result.status = SearchStatus::FAILED;
        return result;
    }

    auto g_value = DiscreteCost(0);
    auto iw1_action_precheck = IW1ActionPrecheckController(options, pruning_strategy, context->get_problem(), start_state);
    const auto emit_novel_witness_events =
        event_handler->supports_novel_witness_events() && pruning_strategy->supports_transition_novel_witness_query();
    auto novel_witness_atom_indices = iw::AtomIndexList {};

    event_handler->on_finish_g_layer(g_value);

    auto stopwatch = StopWatch(options.max_time_in_ms);
    stopwatch.start();

    if (!layer_ordering_strategy)
    {
        auto iw1_incremental_action_discovery = IW1IncrementalActionDiscoveryController(context, options);
        auto iw1_incremental_statistics_reporter = IW1IncrementalStatisticsReporter { event_handler, &iw1_incremental_action_discovery };
        auto queue = std::deque<PackedState>();
        auto candidate_actions = std::vector<GroundAction> {};
        queue.emplace_back(start_state.get_packed_state());

        const auto collect_full_applicable_actions = [&](const State& state) -> std::span<const GroundAction>
        {
            candidate_actions.clear();
            for (const auto& action : applicable_action_generator.create_applicable_action_generator(state))
            {
                candidate_actions.push_back(action);
                if (iw1_incremental_action_discovery.is_enabled() && (state.get_index() == start_state.get_index()))
                {
                    iw1_incremental_action_discovery.on_root_action_fully_enumerated();
                }
            }
            return candidate_actions;
        };

        const auto filter_candidate_actions = [&](const State& state,
                                                  const std::span<const GroundAction>& raw_actions,
                                                  bool is_incremental_non_root) -> std::span<const GroundAction>
        {
            if (!iw1_action_precheck.is_enabled())
            {
                return raw_actions;
            }

            const auto filtered_actions = iw1_action_precheck.filter_actions(state, raw_actions, state_repository);
            if (is_incremental_non_root && iw1_incremental_first_applicability_debug_crosscheck)
            {
                run_incremental_precheck_filtered_crosscheck(context,
                                                             options,
                                                             start_state,
                                                             state,
                                                             pruning_strategy,
                                                             state_repository,
                                                             iw1_incremental_action_discovery,
                                                             filtered_actions);
            }
            return filtered_actions;
        };

        const auto handle_surviving_action = [&](const State& state, SearchNode& search_node, GroundAction action) -> bool
        {
            if (iw1_incremental_action_discovery.is_enabled())
            {
                iw1_incremental_action_discovery.mark_action_tested(action);
            }

            const auto [successor_state, successor_state_metric_value] = state_repository.get_or_create_successor_state(state, action, search_node.g_value);
            auto& successor_search_node = get_or_create_search_node(successor_state.get_index(), search_nodes);
            auto action_cost = successor_state_metric_value - search_node.g_value;

            if (emit_novel_witness_events)
            {
                pruning_strategy->compute_transition_novel_fluent_atom_indices_read_only(state, successor_state, novel_witness_atom_indices);
                event_handler->on_generate_state_with_novel_witness(state, action, action_cost, successor_state, novel_witness_atom_indices);
            }
            event_handler->on_generate_state(state, action, action_cost, successor_state);
            if (pruning_strategy->test_prune_successor_state(state, successor_state, (successor_search_node.status == SearchNodeStatus::NEW)))
            {
                event_handler->on_generate_state_not_in_search_tree(state, action, action_cost, successor_state);
                return true;
            }
            event_handler->on_generate_state_in_search_tree(state, action, action_cost, successor_state);

            successor_search_node.status = SearchNodeStatus::OPEN;
            successor_search_node.parent_state = state.get_index();
            successor_search_node.incoming_action = action->get_index();
            successor_search_node.g_value = search_node.g_value + 1;

            queue.emplace_back(successor_state.get_packed_state());

            if (search_nodes.size() >= options.max_num_states)
            {
                result.status = SearchStatus::OUT_OF_STATES;
                return false;
            }

            return true;
        };

        while (!queue.empty())
        {
            if (stopwatch.has_finished())
            {
                result.status = SearchStatus::OUT_OF_TIME;
                return result;
            }

            const auto state = state_repository.get_state(*queue.front());
            queue.pop_front();

            auto& search_node = get_or_create_search_node(state.get_index(), search_nodes);

            if (search_node.status == SearchNodeStatus::CLOSED || search_node.status == SearchNodeStatus::DEAD_END)
            {
                continue;
            }

            if (search_node.g_value > g_value)
            {
                applicable_action_generator.on_finish_search_layer();
                state_repository.get_axiom_evaluator()->on_finish_search_layer();
                event_handler->on_finish_g_layer(g_value);
                g_value = search_node.g_value;
            }

            if (goal_strategy->test_dynamic_goal(state))
            {
                event_handler->on_expand_goal_state(state);

                if (options.stop_if_goal)
                {
                    event_handler->on_end_search(state_repository.get_reached_fluent_ground_atoms_bitset().count(),
                                                 state_repository.get_reached_derived_ground_atoms_bitset().count(),
                                                 state_repository.get_state_count(),
                                                 search_nodes.size(),
                                                 ground_action_repository.size(),
                                                 ground_axiom_repository.size());

                    applicable_action_generator.on_end_search();
                    state_repository.get_axiom_evaluator()->on_end_search();

                    result.goal_state = state;
                    result.plan = extract_total_ordered_plan(start_state, start_g_value, search_node, state.get_index(), search_nodes, context);
                    result.status = SearchStatus::SOLVED;

                    event_handler->on_solved(result.plan.value());

                    return result;
                }
            }

            event_handler->on_expand_state(state);
            search_node.status = SearchNodeStatus::CLOSED;

            if (pruning_strategy->consume_skip_state_expansion(state))
            {
                continue;
            }

            if (use_max_depth && (search_node.g_value >= max_depth))
            {
                continue;
            }

            if (iw1_incremental_action_discovery.is_enabled())
            {
                const auto is_incremental_non_root = (state.get_index() != start_state.get_index());
                const auto raw_actions = is_incremental_non_root ? iw1_incremental_action_discovery.get_actions_to_expand(state, search_node) :
                                                                   collect_full_applicable_actions(state);
                const auto filtered_actions = filter_candidate_actions(state, raw_actions, is_incremental_non_root);
                for (const auto& action : filtered_actions)
                {
                    if (!handle_surviving_action(state, search_node, action))
                    {
                        return result;
                    }
                }
                continue;
            }

            if (iw1_action_precheck.is_enabled())
            {
                if (iw1_action_precheck.supports_online_filtering())
                {
                    for (const auto& action : applicable_action_generator.create_applicable_action_generator(state))
                    {
                        if (!iw1_action_precheck.test_action(state, action, state_repository))
                        {
                            continue;
                        }

                        if (!handle_surviving_action(state, search_node, action))
                        {
                            return result;
                        }
                    }
                }
                else
                {
                    auto applicable_actions = std::vector<GroundAction> {};
                    for (const auto& action : applicable_action_generator.create_applicable_action_generator(state))
                    {
                        applicable_actions.push_back(action);
                    }
                    const auto filtered_actions = iw1_action_precheck.filter_actions(state, applicable_actions, state_repository);
                    for (const auto& action : filtered_actions)
                    {
                        if (!handle_surviving_action(state, search_node, action))
                        {
                            return result;
                        }
                    }
                }
            }
            else
            {
                for (const auto& action : applicable_action_generator.create_applicable_action_generator(state))
                {
                    if (!handle_surviving_action(state, search_node, action))
                    {
                        return result;
                    }
                }
            }
        }
    }
    else if (use_beam)
    {
        return find_solution_with_beam(context,
                                       options,
                                       start_state,
                                       start_g_value,
                                       event_handler,
                                       goal_strategy,
                                       pruning_strategy,
                                       layer_ordering_strategy,
                                       search_nodes,
                                       g_value,
                                       stopwatch);
    }
    else
    {
        return find_solution_with_ordered_layer(context,
                                                options,
                                                start_state,
                                                start_g_value,
                                                event_handler,
                                                goal_strategy,
                                                pruning_strategy,
                                                layer_ordering_strategy,
                                                search_nodes,
                                                g_value,
                                                stopwatch);
    }

    event_handler->on_end_search(state_repository.get_reached_fluent_ground_atoms_bitset().count(),
                                 state_repository.get_reached_derived_ground_atoms_bitset().count(),
                                 state_repository.get_state_count(),
                                 search_nodes.size(),
                                 ground_action_repository.size(),
                                 ground_axiom_repository.size());
    event_handler->on_exhausted();

    result.status = SearchStatus::EXHAUSTED;
    return result;
}
}
