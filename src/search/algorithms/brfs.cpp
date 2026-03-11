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

#include "mimir/common/timers.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/search/algorithms/brfs/event_handlers.hpp"
#include "mimir/search/algorithms/brfs/event_handlers/interface.hpp"
#include "mimir/search/algorithms/strategies/goal_strategy.hpp"
#include "mimir/search/algorithms/strategies/layer_ordering_strategy.hpp"
#include "mimir/search/algorithms/strategies/pruning_strategy.hpp"
#include "mimir/search/applicable_action_generators/interface.hpp"
#include "mimir/search/axiom_evaluators/interface.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/search_space.hpp"
#include "mimir/search/state_repository.hpp"

#include <algorithm>
#include <deque>
#include <stdexcept>

using namespace mimir::formalism;

namespace mimir::search::brfs
{
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
    const auto parallel_beam_num_threads = options.parallel_beam_num_threads;

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

    if (parallel_beam_num_threads > 1)
    {
        if (!use_beam)
        {
            throw std::invalid_argument("BrFS::Options.parallel_beam_num_threads requires BrFS::Options.beam_width.");
        }

        if (!applicable_action_generator.supports_parallel_beam() || !state_repository.get_axiom_evaluator()->supports_parallel_beam())
        {
            throw std::invalid_argument("BrFS::Options.parallel_beam_num_threads currently requires grounded search contexts.");
        }
    }

    auto result = SearchResult();
    auto search_nodes = SearchNodeVector();

    auto& start_search_node = get_or_create_search_node(start_state.get_index(), search_nodes);
    start_search_node.status = SearchNodeStatus::OPEN;
    start_search_node.g_value = 0;

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

    event_handler->on_finish_g_layer(g_value);

    auto stopwatch = StopWatch(options.max_time_in_ms);
    stopwatch.start();

    if (!layer_ordering_strategy)
    {
        auto queue = std::deque<PackedState>();
        queue.emplace_back(start_state.get_packed_state());

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

            for (const auto& action : applicable_action_generator.create_applicable_action_generator(state))
            {
                const auto [successor_state, successor_state_metric_value] = state_repository.get_or_create_successor_state(state, action, search_node.g_value);
                auto& successor_search_node = get_or_create_search_node(successor_state.get_index(), search_nodes);
                auto action_cost = successor_state_metric_value - search_node.g_value;

                event_handler->on_generate_state(state, action, action_cost, successor_state);
                if (pruning_strategy->test_prune_successor_state(state, successor_state, (successor_search_node.status == SearchNodeStatus::NEW)))
                {
                    event_handler->on_generate_state_not_in_search_tree(state, action, action_cost, successor_state);
                    continue;
                }
                event_handler->on_generate_state_in_search_tree(state, action, action_cost, successor_state);

                successor_search_node.status = SearchNodeStatus::OPEN;
                successor_search_node.parent_state = state.get_index();
                successor_search_node.g_value = search_node.g_value + 1;

                queue.emplace_back(successor_state.get_packed_state());

                if (search_nodes.size() >= options.max_num_states)
                {
                    result.status = SearchStatus::OUT_OF_STATES;
                    return result;
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
