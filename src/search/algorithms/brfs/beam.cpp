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
#include "mimir/algorithms/BS_thread_pool.hpp"

#include <algorithm>
#include <future>
#include <memory>
#include <random>

using namespace mimir::formalism;

namespace mimir::search::brfs
{
namespace
{
struct BeamCandidate
{
    State parent_state;
    GroundAction action;
    ContinuousCost action_cost;
    State successor_state;
    DiscreteCost successor_g_value;
    ContinuousCost score;
    uint64_t generation_sequence;
    uint64_t tie_token;
    bool is_new_successor;
};

struct BeamRanking
{
    bool prefer_higher_scores;
    bool randomize_equal_score_ties;

    bool better(const BeamCandidate& lhs, const BeamCandidate& rhs) const
    {
        if (lhs.score != rhs.score)
        {
            return prefer_higher_scores ? (lhs.score > rhs.score) : (lhs.score < rhs.score);
        }

        if (randomize_equal_score_ties && (lhs.tie_token != rhs.tie_token))
        {
            return lhs.tie_token < rhs.tie_token;
        }

        return lhs.generation_sequence < rhs.generation_sequence;
    }
};

struct BeamHeapCompare
{
    BeamRanking ranking;

    bool operator()(const BeamCandidate& lhs, const BeamCandidate& rhs) const { return ranking.better(lhs, rhs); }
};

struct ParallelBeamTaskInput
{
    const State* parent_state;
    GroundAction action;
    ContinuousCost parent_metric_value;
    DiscreteCost successor_g_value;
    uint64_t generation_sequence;
};

struct ParallelBeamEvaluatedCandidate
{
    ParallelBeamTaskInput task;
    StateRepositoryImpl::StagedSuccessorState successor_state;
    ContinuousCost action_cost;
    ContinuousCost score;
    bool pruned_for_selection;
};

void reject_beam_candidate(const BeamCandidate& candidate, const EventHandler& event_handler)
{
    event_handler->on_generate_state_not_in_search_tree(candidate.parent_state, candidate.action, candidate.action_cost, candidate.successor_state);
}

void admit_beam_candidate(const BeamCandidate& candidate, SearchNodeVector& search_nodes, StateList& next_layer, const EventHandler& event_handler)
{
    auto& successor_search_node = get_or_create_search_node(candidate.successor_state.get_index(), search_nodes);
    successor_search_node.status = SearchNodeStatus::OPEN;
    successor_search_node.parent_state = candidate.parent_state.get_index();
    successor_search_node.g_value = candidate.successor_g_value;

    next_layer.emplace_back(candidate.successor_state);
    event_handler->on_generate_state_in_search_tree(candidate.parent_state, candidate.action, candidate.action_cost, candidate.successor_state);
}

void add_candidate_to_small_beam(std::vector<BeamCandidate>& beam_candidates,
                                 BeamCandidate candidate,
                                 size_t beam_width,
                                 const BeamRanking& ranking,
                                 const EventHandler& event_handler)
{
    if ((beam_candidates.size() >= beam_width) && !ranking.better(candidate, beam_candidates.back()))
    {
        reject_beam_candidate(candidate, event_handler);
        return;
    }

    auto insert_it = beam_candidates.begin();
    while ((insert_it != beam_candidates.end()) && ranking.better(*insert_it, candidate))
    {
        ++insert_it;
    }
    beam_candidates.insert(insert_it, std::move(candidate));

    if (beam_candidates.size() > beam_width)
    {
        auto evicted_candidate = std::move(beam_candidates.back());
        beam_candidates.pop_back();
        reject_beam_candidate(evicted_candidate, event_handler);
    }
}

void add_candidate_to_heap_beam(std::vector<BeamCandidate>& beam_candidates,
                                BeamCandidate candidate,
                                size_t beam_width,
                                const BeamRanking& ranking,
                                const BeamHeapCompare& heap_compare,
                                const EventHandler& event_handler)
{
    if (beam_candidates.size() < beam_width)
    {
        beam_candidates.push_back(std::move(candidate));
        std::push_heap(beam_candidates.begin(), beam_candidates.end(), heap_compare);
        return;
    }

    if (!ranking.better(candidate, beam_candidates.front()))
    {
        reject_beam_candidate(candidate, event_handler);
        return;
    }

    std::pop_heap(beam_candidates.begin(), beam_candidates.end(), heap_compare);
    auto evicted_candidate = std::move(beam_candidates.back());
    beam_candidates.pop_back();
    reject_beam_candidate(evicted_candidate, event_handler);

    beam_candidates.push_back(std::move(candidate));
    std::push_heap(beam_candidates.begin(), beam_candidates.end(), heap_compare);
}

void sort_beam_candidates(std::vector<BeamCandidate>& beam_candidates, const BeamRanking& ranking)
{
    std::sort(beam_candidates.begin(), beam_candidates.end(), [&ranking](const auto& lhs, const auto& rhs) { return ranking.better(lhs, rhs); });
}

void finalize_beam_layer(const EventHandler& event_handler,
                         const PruningStrategy& pruning_strategy,
                         BeamNoveltyMode beam_novelty_mode,
                         SearchNodeVector& search_nodes,
                         std::vector<BeamCandidate>& beam_candidates,
                         StateList& next_layer)
{
    next_layer.clear();
    next_layer.reserve(beam_candidates.size());

    // In SURVIVORS_ONLY, only kept beam states are allowed to update the novelty table.
    if (beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY)
    {
        pruning_strategy->on_begin_beam_replay(beam_novelty_mode);
    }

    for (const auto& candidate : beam_candidates)
    {
        if ((beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY)
            && pruning_strategy->test_prune_successor_state_for_beam_replay(candidate.parent_state,
                                                                            candidate.successor_state,
                                                                            candidate.is_new_successor,
                                                                            beam_novelty_mode))
        {
            reject_beam_candidate(candidate, event_handler);
            continue;
        }

        admit_beam_candidate(candidate, search_nodes, next_layer, event_handler);
    }

    if (beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY)
    {
        pruning_strategy->on_end_beam_replay(beam_novelty_mode);
    }
}
}  // namespace

SearchResult find_solution_with_beam(const SearchContext& context,
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
    // True beam mode still respects BFS layers. We generate the full candidate set for
    // depth d+1, novelty-check each successor first, score the novel ones eagerly, and
    // keep only the best `beam_width` states for the next layer.
    const auto& problem = *context->get_problem();
    auto& applicable_action_generator = *context->get_applicable_action_generator();
    auto& state_repository = *context->get_state_repository();
    const auto& ground_action_repository = boost::hana::at_key(problem.get_repositories().get_hana_repositories(), boost::hana::type<GroundActionImpl> {});
    const auto& ground_axiom_repository = boost::hana::at_key(problem.get_repositories().get_hana_repositories(), boost::hana::type<GroundAxiomImpl> {});

    auto result = SearchResult();

    const auto beam_width = options.beam_width;
    const auto beam_novelty_mode = options.beam_novelty_mode;
    const auto parallel_beam_num_threads = options.parallel_beam_num_threads;
    const auto use_parallel_beam = parallel_beam_num_threads > 1;
    const auto prefer_higher_scores = layer_ordering_strategy->prefer_higher_scores();
    const auto use_small_beam = (beam_width <= 64);
    const auto ranking = BeamRanking { prefer_higher_scores, options.randomize_equal_score_ties };
    const auto heap_compare = BeamHeapCompare { ranking };
    auto tie_break_rng = std::mt19937_64(options.equal_score_tie_seed);
    auto generated_state_indices = UnorderedSet<Index> {};
    generated_state_indices.insert(start_state.get_index());

    auto current_layer = StateList {};
    auto next_layer = StateList {};
    auto beam_candidates = std::vector<BeamCandidate> {};
    auto parallel_chunk_tasks = std::vector<ParallelBeamTaskInput> {};
    auto parallel_beam_pool = std::unique_ptr<BS::thread_pool> {};
    current_layer.push_back(start_state);

    if (use_parallel_beam)
    {
        parallel_beam_pool = std::make_unique<BS::thread_pool>(parallel_beam_num_threads);
        parallel_chunk_tasks.reserve(1024);
    }

    while (!current_layer.empty())
    {
        beam_candidates.clear();
        beam_candidates.reserve(std::min<size_t>(beam_width, current_layer.size()));
        auto generation_sequence = uint64_t(0);
        auto candidate_generation_sequence = uint64_t(0);

        auto flush_parallel_chunk = [&]() -> bool
        {
            if (parallel_chunk_tasks.empty())
            {
                return true;
            }

            // Workers only compute successor contents, run the read-only selection test,
            // and score surviving candidates. Canonical state insertion, duplicate checks,
            // beam updates, and event callbacks stay serial in generation order.
            auto futures = std::vector<std::future<ParallelBeamEvaluatedCandidate>> {};
            futures.reserve(parallel_chunk_tasks.size());

            for (const auto& task : parallel_chunk_tasks)
            {
                futures.push_back(parallel_beam_pool->submit_task([&state_repository,
                                                                   &layer_ordering_strategy,
                                                                   &pruning_strategy,
                                                                   beam_novelty_mode,
                                                                   task]()
                                                                  {
                                                                      thread_local auto scratch =
                                                                          std::make_unique<StateRepositoryImpl::StagedSuccessorScratch>();

                                                                      auto successor_state =
                                                                          state_repository.compute_staged_successor_state(*task.parent_state,
                                                                                                                         task.action,
                                                                                                                         task.parent_metric_value,
                                                                                                                         *scratch);
                                                                      auto temporary_successor_state = state_repository.make_temporary_staged_successor_state(
                                                                          successor_state,
                                                                          static_cast<Index>(task.generation_sequence),
                                                                          *scratch);
                                                                      const auto action_cost = successor_state.metric_value - task.parent_metric_value;
                                                                      const auto pruned_for_selection =
                                                                          pruning_strategy->test_prune_successor_state_for_beam_selection(*task.parent_state,
                                                                                                                                          temporary_successor_state,
                                                                                                                                          true,
                                                                                                                                          beam_novelty_mode);

                                                                      auto evaluated_candidate = ParallelBeamEvaluatedCandidate { task,
                                                                                                                                    std::move(successor_state),
                                                                                                                                    action_cost,
                                                                                                                                    0.0,
                                                                                                                                    pruned_for_selection };
                                                                      if (!evaluated_candidate.pruned_for_selection)
                                                                      {
                                                                          evaluated_candidate.score =
                                                                              layer_ordering_strategy->score_state(temporary_successor_state,
                                                                                                                   evaluated_candidate.task.successor_g_value);
                                                                      }

                                                                      return evaluated_candidate;
                                                                  }));
            }

            for (auto& future : futures)
            {
                auto evaluated_candidate = future.get();
                const auto [successor_state, successor_state_metric_value] =
                    state_repository.get_or_create_staged_successor_state(evaluated_candidate.successor_state);
                [[maybe_unused]] auto& successor_search_node = get_or_create_search_node(successor_state.get_index(), search_nodes);
                [[maybe_unused]] const auto ignored_successor_state_metric_value = successor_state_metric_value;
                const auto is_new_successor = generated_state_indices.insert(successor_state.get_index()).second;

                event_handler->on_generate_state(*evaluated_candidate.task.parent_state,
                                                 evaluated_candidate.task.action,
                                                 evaluated_candidate.action_cost,
                                                 successor_state);
                if (!is_new_successor || evaluated_candidate.pruned_for_selection)
                {
                    event_handler->on_generate_state_not_in_search_tree(*evaluated_candidate.task.parent_state,
                                                                        evaluated_candidate.task.action,
                                                                        evaluated_candidate.action_cost,
                                                                        successor_state);
                    continue;
                }

                auto candidate = BeamCandidate { *evaluated_candidate.task.parent_state,
                                                evaluated_candidate.task.action,
                                                evaluated_candidate.action_cost,
                                                successor_state,
                                                evaluated_candidate.task.successor_g_value,
                                                evaluated_candidate.score,
                                                candidate_generation_sequence++,
                                                options.randomize_equal_score_ties ? tie_break_rng() : uint64_t(0),
                                                is_new_successor };

                if (use_small_beam)
                {
                    add_candidate_to_small_beam(beam_candidates, std::move(candidate), beam_width, ranking, event_handler);
                }
                else
                {
                    add_candidate_to_heap_beam(beam_candidates, std::move(candidate), beam_width, ranking, heap_compare, event_handler);
                }

                if (search_nodes.size() >= options.max_num_states)
                {
                    result.status = SearchStatus::OUT_OF_STATES;
                    return false;
                }
            }

            parallel_chunk_tasks.clear();
            return true;
        };

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

            for (const auto& action : applicable_action_generator.create_applicable_action_generator(state))
            {
                if (!use_parallel_beam)
                {
                    const auto [successor_state, successor_state_metric_value] =
                        state_repository.get_or_create_successor_state(state, action, search_node.g_value);
                    [[maybe_unused]] auto& successor_search_node = get_or_create_search_node(successor_state.get_index(), search_nodes);
                    auto action_cost = successor_state_metric_value - search_node.g_value;
                    const auto successor_g_value = search_node.g_value + 1;
                    const auto is_new_successor = generated_state_indices.insert(successor_state.get_index()).second;

                    event_handler->on_generate_state(state, action, action_cost, successor_state);
                    if (pruning_strategy->test_prune_successor_state_for_beam_selection(state, successor_state, is_new_successor, beam_novelty_mode))
                    {
                        event_handler->on_generate_state_not_in_search_tree(state, action, action_cost, successor_state);
                        continue;
                    }

                    auto candidate = BeamCandidate { state,
                                                    action,
                                                    action_cost,
                                                    successor_state,
                                                    successor_g_value,
                                                    layer_ordering_strategy->score_state(successor_state, successor_g_value),
                                                    generation_sequence++,
                                                    options.randomize_equal_score_ties ? tie_break_rng() : uint64_t(0),
                                                    is_new_successor };

                    if (use_small_beam)
                    {
                        add_candidate_to_small_beam(beam_candidates, std::move(candidate), beam_width, ranking, event_handler);
                    }
                    else
                    {
                        add_candidate_to_heap_beam(beam_candidates, std::move(candidate), beam_width, ranking, heap_compare, event_handler);
                    }

                    if (search_nodes.size() >= options.max_num_states)
                    {
                        result.status = SearchStatus::OUT_OF_STATES;
                        return result;
                    }

                    continue;
                }

                parallel_chunk_tasks.push_back(ParallelBeamTaskInput { &state,
                                                                      action,
                                                                      static_cast<ContinuousCost>(search_node.g_value),
                                                                      search_node.g_value + 1,
                                                                      generation_sequence++ });
                if (parallel_chunk_tasks.size() == 1024)
                {
                    if (!flush_parallel_chunk())
                    {
                        return result;
                    }
                }
            }
        }

        if (use_parallel_beam && !flush_parallel_chunk())
        {
            return result;
        }

        if (!use_small_beam)
        {
            sort_beam_candidates(beam_candidates, ranking);
        }

        finalize_beam_layer(event_handler, pruning_strategy, beam_novelty_mode, search_nodes, beam_candidates, next_layer);

        if (next_layer.empty())
        {
            break;
        }

        applicable_action_generator.on_finish_search_layer();
        state_repository.get_axiom_evaluator()->on_finish_search_layer();
        event_handler->on_finish_g_layer(g_value);

        ++g_value;
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
