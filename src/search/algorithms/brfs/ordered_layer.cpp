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

#include "internal.hpp"

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

using namespace mimir::formalism;

namespace mimir::search::brfs
{
SearchResult find_solution_with_ordered_layer(const SearchContext& context,
                                              const Options& options,
                                              const State& start_state,
                                              ContinuousCost start_g_value,
                                              const EventHandler& event_handler,
                                              const GoalStrategy& goal_strategy,
                                              const PruningStrategy& pruning_strategy,
                                              const LayerOrderingStrategy& layer_ordering_strategy,
                                              SearchNodeVector& search_nodes,
                                              DiscreteCost g_value,
                                              StopWatch& stopwatch)
{
    const auto& problem = *context->get_problem();
    auto& applicable_action_generator = *context->get_applicable_action_generator();
    auto& state_repository = *context->get_state_repository();
    const auto& ground_action_repository = boost::hana::at_key(problem.get_repositories().get_hana_repositories(), boost::hana::type<GroundActionImpl> {});
    const auto& ground_axiom_repository = boost::hana::at_key(problem.get_repositories().get_hana_repositories(), boost::hana::type<GroundAxiomImpl> {});

    auto result = SearchResult();
    const auto max_next_layer_states = options.max_next_layer_states;
    const auto use_next_layer_limit = (max_next_layer_states < std::numeric_limits<uint32_t>::max());
    const auto max_depth = options.max_depth;
    const auto use_max_depth = (max_depth < std::numeric_limits<uint32_t>::max());
    auto iw1_action_precheck = IW1ActionPrecheckController(options, pruning_strategy, context->get_problem(), start_state);

    struct ScoredState
    {
        State state;
        ContinuousCost score;
    };

    const auto use_eager_successor_scoring = use_next_layer_limit && layer_ordering_strategy->supports_eager_scoring();
    const auto prefer_higher_scores = use_eager_successor_scoring && layer_ordering_strategy->prefer_higher_scores();
    const auto emit_novel_witness_events =
        event_handler->supports_novel_witness_events() && pruning_strategy->supports_transition_novel_witness_query();
    auto novel_witness_atom_indices = iw::AtomIndexList {};

    auto current_layer = StateList {};
    auto next_layer = StateList {};
    auto scored_next_layer = std::vector<ScoredState> {};
    current_layer.push_back(start_state);

    while (!current_layer.empty())
    {
        auto next_layer_limit_reached = false;

        for (const auto& state : current_layer)
        {
            if (stopwatch.has_finished())
            {
                result.status = SearchStatus::OUT_OF_TIME;
                return result;
            }

            auto& search_node = get_or_create_search_node(state.get_index(), search_nodes);

            if (search_node.status == SearchNodeStatus::CLOSED || search_node.status == SearchNodeStatus::DEAD_END)
            {
                continue;
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

                        const auto [successor_state, successor_state_metric_value] =
                            state_repository.get_or_create_successor_state(state, action, search_node.g_value);
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
                            continue;
                        }
                        event_handler->on_generate_state_in_search_tree(state, action, action_cost, successor_state);

                        successor_search_node.status = SearchNodeStatus::OPEN;
                        successor_search_node.parent_state = state.get_index();
                        successor_search_node.g_value = search_node.g_value + 1;

                        if (use_eager_successor_scoring)
                        {
                            const auto successor_score = layer_ordering_strategy->score_state(successor_state, successor_search_node.g_value);
                            const auto insert_it = std::upper_bound(scored_next_layer.begin(),
                                                                   scored_next_layer.end(),
                                                                   successor_score,
                                                                   [prefer_higher_scores](ContinuousCost lhs, const ScoredState& rhs)
                                                                   {
                                                                       return prefer_higher_scores ? (lhs > rhs.score) : (lhs < rhs.score);
                                                                   });
                            scored_next_layer.insert(insert_it, ScoredState { successor_state, successor_score });
                            next_layer_limit_reached = (scored_next_layer.size() >= max_next_layer_states);
                        }
                        else
                        {
                            next_layer.emplace_back(successor_state);
                            next_layer_limit_reached = use_next_layer_limit && (next_layer.size() >= max_next_layer_states);
                        }

                        if (search_nodes.size() >= options.max_num_states)
                        {
                            result.status = SearchStatus::OUT_OF_STATES;
                            return result;
                        }

                        if (next_layer_limit_reached)
                        {
                            break;
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
                        const auto [successor_state, successor_state_metric_value] =
                            state_repository.get_or_create_successor_state(state, action, search_node.g_value);
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
                            continue;
                        }
                        event_handler->on_generate_state_in_search_tree(state, action, action_cost, successor_state);

                        successor_search_node.status = SearchNodeStatus::OPEN;
                        successor_search_node.parent_state = state.get_index();
                        successor_search_node.g_value = search_node.g_value + 1;

                        if (use_eager_successor_scoring)
                        {
                            const auto successor_score = layer_ordering_strategy->score_state(successor_state, successor_search_node.g_value);
                            const auto insert_it = std::upper_bound(scored_next_layer.begin(),
                                                                   scored_next_layer.end(),
                                                                   successor_score,
                                                                   [prefer_higher_scores](ContinuousCost lhs, const ScoredState& rhs)
                                                                   {
                                                                       return prefer_higher_scores ? (lhs > rhs.score) : (lhs < rhs.score);
                                                                   });
                            scored_next_layer.insert(insert_it, ScoredState { successor_state, successor_score });
                            next_layer_limit_reached = (scored_next_layer.size() >= max_next_layer_states);
                        }
                        else
                        {
                            next_layer.emplace_back(successor_state);
                            next_layer_limit_reached = use_next_layer_limit && (next_layer.size() >= max_next_layer_states);
                        }

                        if (search_nodes.size() >= options.max_num_states)
                        {
                            result.status = SearchStatus::OUT_OF_STATES;
                            return result;
                        }

                        if (next_layer_limit_reached)
                        {
                            break;
                        }
                    }
                }
            }
            else
            {
                for (const auto& action : applicable_action_generator.create_applicable_action_generator(state))
                {
                    const auto [successor_state, successor_state_metric_value] =
                        state_repository.get_or_create_successor_state(state, action, search_node.g_value);
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
                        continue;
                    }
                    event_handler->on_generate_state_in_search_tree(state, action, action_cost, successor_state);

                    successor_search_node.status = SearchNodeStatus::OPEN;
                    successor_search_node.parent_state = state.get_index();
                    successor_search_node.g_value = search_node.g_value + 1;

                    if (use_eager_successor_scoring)
                    {
                        const auto successor_score = layer_ordering_strategy->score_state(successor_state, successor_search_node.g_value);
                        const auto insert_it = std::upper_bound(scored_next_layer.begin(),
                                                               scored_next_layer.end(),
                                                               successor_score,
                                                               [prefer_higher_scores](ContinuousCost lhs, const ScoredState& rhs)
                                                               {
                                                                   return prefer_higher_scores ? (lhs > rhs.score) : (lhs < rhs.score);
                                                               });
                        scored_next_layer.insert(insert_it, ScoredState { successor_state, successor_score });
                        next_layer_limit_reached = (scored_next_layer.size() >= max_next_layer_states);
                    }
                    else
                    {
                        next_layer.emplace_back(successor_state);
                        next_layer_limit_reached = use_next_layer_limit && (next_layer.size() >= max_next_layer_states);
                    }

                    if (search_nodes.size() >= options.max_num_states)
                    {
                        result.status = SearchStatus::OUT_OF_STATES;
                        return result;
                    }

                    if (next_layer_limit_reached)
                    {
                        break;
                    }
                }
            }

            if (next_layer_limit_reached)
            {
                break;
            }
        }

        if (use_eager_successor_scoring)
        {
            next_layer.clear();
            next_layer.reserve(scored_next_layer.size());
            for (auto& scored_state : scored_next_layer)
            {
                next_layer.emplace_back(std::move(scored_state.state));
            }
            scored_next_layer.clear();
        }

        if (next_layer.empty())
        {
            break;
        }

        applicable_action_generator.on_finish_search_layer();
        state_repository.get_axiom_evaluator()->on_finish_search_layer();
        event_handler->on_finish_g_layer(g_value);

        ++g_value;
        layer_ordering_strategy->order_layer(next_layer, g_value);
        current_layer = std::move(next_layer);
        next_layer.clear();
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
