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
#include <chrono>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <random>

using namespace mimir::formalism;

namespace mimir::search::brfs
{
namespace
{
struct BeamCandidate
{
    const State* parent_state;
    GroundAction action;
    ContinuousCost action_cost;
    State successor_state;
    DiscreteCost successor_g_value;
    ContinuousCost score;
    uint64_t generation_sequence;
    uint64_t tie_token;
    bool is_new_successor;
};

struct DeferredBeamCandidate
{
    const State* parent_state;
    GroundAction action;
    ContinuousCost action_cost;
    StateRepositoryImpl::StagedSuccessorState successor_state;
    StateRepositoryImpl::StagedSuccessorHandle successor_handle;
    DiscreteCost successor_g_value;
    ContinuousCost score;
    uint64_t generation_sequence;
    uint64_t tie_token;
    bool is_new_successor;
};

struct RelaxedDeferredBeamCandidate
{
    const State* parent_state;
    GroundAction action;
    ContinuousCost action_cost;
    StateRepositoryImpl::StagedSuccessorState successor_state;
    DiscreteCost successor_g_value;
    ContinuousCost score;
    uint64_t generation_sequence;
    uint64_t tie_token;
};

struct BeamRanking
{
    bool prefer_higher_scores;
    bool randomize_equal_score_ties;

    template<typename Candidate>
    bool better(const Candidate& lhs, const Candidate& rhs) const
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

template<typename Candidate>
struct BeamHeapCompare
{
    BeamRanking ranking;

    bool operator()(const Candidate& lhs, const Candidate& rhs) const { return ranking.better(lhs, rhs); }
};

struct ParallelBeamTaskInput
{
    const State* parent_state;
    GroundAction action;
    ContinuousCost parent_metric_value;
    DiscreteCost successor_g_value;
    uint64_t generation_sequence;
    uint64_t tie_token;
};

struct ParallelBeamEvaluatedCandidate
{
    ParallelBeamTaskInput task;
    StateRepositoryImpl::StagedSuccessorState successor_state;
    ContinuousCost action_cost;
    ContinuousCost score;
};

struct EvaluatedChunk
{
    uint64_t chunk_id;
    uint64_t first_generation_sequence;
    std::vector<ParallelBeamEvaluatedCandidate> candidates;
    std::chrono::nanoseconds worker_compute_time;
};

struct RelaxedPartitionResult
{
    std::vector<RelaxedDeferredBeamCandidate> candidates;
    std::chrono::nanoseconds worker_compute_time;
    size_t task_count;
};

struct InFlightChunk
{
    uint64_t chunk_id;
    std::future<EvaluatedChunk> future;
};

void reject_beam_candidate(const BeamCandidate& candidate, const EventHandler& event_handler)
{
    event_handler->on_generate_state_not_in_search_tree(*candidate.parent_state, candidate.action, candidate.action_cost, candidate.successor_state);
}

void admit_beam_candidate(const BeamCandidate& candidate, SearchNodeVector& search_nodes, StateList& next_layer, const EventHandler& event_handler)
{
    auto& successor_search_node = get_or_create_search_node(candidate.successor_state.get_index(), search_nodes);
    successor_search_node.status = SearchNodeStatus::OPEN;
    successor_search_node.parent_state = candidate.parent_state->get_index();
    successor_search_node.g_value = candidate.successor_g_value;

    next_layer.emplace_back(candidate.successor_state);
    event_handler->on_generate_state_in_search_tree(*candidate.parent_state, candidate.action, candidate.action_cost, candidate.successor_state);
}

template<typename Candidate, typename RejectFn>
void add_candidate_to_small_beam(std::vector<Candidate>& beam_candidates,
                                 Candidate candidate,
                                 size_t beam_width,
                                 const BeamRanking& ranking,
                                 RejectFn&& reject_candidate)
{
    if ((beam_candidates.size() >= beam_width) && !ranking.better(candidate, beam_candidates.back()))
    {
        reject_candidate(candidate);
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
        reject_candidate(evicted_candidate);
    }
}

template<typename Candidate, typename RejectFn>
void add_candidate_to_heap_beam(std::vector<Candidate>& beam_candidates,
                                Candidate candidate,
                                size_t beam_width,
                                const BeamRanking& ranking,
                                const BeamHeapCompare<Candidate>& heap_compare,
                                RejectFn&& reject_candidate)
{
    if (beam_candidates.size() < beam_width)
    {
        beam_candidates.push_back(std::move(candidate));
        std::push_heap(beam_candidates.begin(), beam_candidates.end(), heap_compare);
        return;
    }

    if (!ranking.better(candidate, beam_candidates.front()))
    {
        reject_candidate(candidate);
        return;
    }

    std::pop_heap(beam_candidates.begin(), beam_candidates.end(), heap_compare);
    auto evicted_candidate = std::move(beam_candidates.back());
    beam_candidates.pop_back();
    reject_candidate(evicted_candidate);

    beam_candidates.push_back(std::move(candidate));
    std::push_heap(beam_candidates.begin(), beam_candidates.end(), heap_compare);
}

template<typename Candidate>
void sort_beam_candidates(std::vector<Candidate>& beam_candidates, const BeamRanking& ranking)
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
            && pruning_strategy->test_prune_successor_state_for_beam_replay(*candidate.parent_state,
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

void finalize_deferred_beam_layer(const EventHandler& event_handler,
                                  const PruningStrategy& pruning_strategy,
                                  BeamNoveltyMode beam_novelty_mode,
                                  StateRepositoryImpl& state_repository,
                                  SearchNodeVector& search_nodes,
                                  std::vector<DeferredBeamCandidate>& beam_candidates,
                                  StateList& next_layer)
{
    next_layer.clear();
    next_layer.reserve(beam_candidates.size());

    if (beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY)
    {
        pruning_strategy->on_begin_beam_replay(beam_novelty_mode);
    }

    for (const auto& candidate : beam_candidates)
    {
        if ((beam_novelty_mode == BeamNoveltyMode::SURVIVORS_ONLY)
            && pruning_strategy->test_prune_staged_successor_state_for_beam_replay(*candidate.parent_state,
                                                                                   candidate.successor_state.fluent_atoms,
                                                                                   candidate.successor_state.derived_atoms,
                                                                                   candidate.successor_state.fluent_numeric_variables,
                                                                                   candidate.successor_state.fluent_atom_indices,
                                                                                   candidate.is_new_successor,
                                                                                   beam_novelty_mode))
        {
            event_handler->on_generate_state_not_in_search_tree_without_payload();
            continue;
        }

        auto successor_state = state_repository.materialize_staged_successor_state(candidate.successor_state, candidate.successor_handle);
        auto& successor_search_node = get_or_create_search_node(successor_state.get_index(), search_nodes);
        successor_search_node.status = SearchNodeStatus::OPEN;
        successor_search_node.parent_state = candidate.parent_state->get_index();
        successor_search_node.g_value = candidate.successor_g_value;

        next_layer.emplace_back(successor_state);
        event_handler->on_generate_state_in_search_tree(*candidate.parent_state, candidate.action, candidate.action_cost, successor_state);
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
    const auto problem_handle = context->get_problem();
    const auto& problem = *problem_handle;
    auto& applicable_action_generator = *context->get_applicable_action_generator();
    auto& state_repository = *context->get_state_repository();
    const auto& ground_action_repository = boost::hana::at_key(problem.get_repositories().get_hana_repositories(), boost::hana::type<GroundActionImpl> {});
    const auto& ground_axiom_repository = boost::hana::at_key(problem.get_repositories().get_hana_repositories(), boost::hana::type<GroundAxiomImpl> {});

    auto result = SearchResult();

    const auto beam_width = options.beam_width;
    const auto beam_novelty_mode = options.beam_novelty_mode;
    const auto use_relaxed_survivors_only_beam = options.relaxed_survivors_only_beam;
    const auto parallel_beam_num_threads = options.parallel_beam_num_threads;
    const auto parallel_beam_chunk_size = options.parallel_beam_chunk_size;
    const auto max_depth = options.max_depth;
    const auto use_max_depth = (max_depth < std::numeric_limits<uint32_t>::max());
    const auto use_parallel_beam = parallel_beam_num_threads > 1;
    const auto use_parallel_action_generation = use_parallel_beam && applicable_action_generator.supports_parallel_applicable_action_generation();
    const auto use_staged_parallel_fast_path =
        use_parallel_beam && event_handler->supports_payloadless_generated_state_events()
        && pruning_strategy->supports_staged_beam_pruning(beam_novelty_mode) && layer_ordering_strategy->supports_staged_scoring();
    const auto use_relaxed_staged_parallel_fast_path =
        use_relaxed_survivors_only_beam && use_parallel_beam && event_handler->supports_payloadless_generated_state_events()
        && pruning_strategy->supports_relaxed_staged_beam_pruning(beam_novelty_mode) && layer_ordering_strategy->supports_staged_scoring();
    const auto use_fused_relaxed_parallel_successor_generation =
        use_relaxed_staged_parallel_fast_path && applicable_action_generator.supports_parallel_relaxed_beam_successor_generation();
    const auto prefer_higher_scores = layer_ordering_strategy->prefer_higher_scores();
    const auto use_small_beam = (beam_width <= 64);
    const auto ranking = BeamRanking { prefer_higher_scores, options.randomize_equal_score_ties };
    const auto heap_compare = BeamHeapCompare<BeamCandidate> { ranking };
    const auto deferred_heap_compare = BeamHeapCompare<DeferredBeamCandidate> { ranking };
    auto tie_break_rng = std::mt19937_64(options.equal_score_tie_seed);
    auto generated_state_indices = UnorderedSet<Index> {};
    generated_state_indices.insert(start_state.get_index());

    auto current_layer = StateList {};
    auto next_layer = StateList {};
    auto beam_candidates = std::vector<BeamCandidate> {};
    auto deferred_beam_candidates = std::vector<DeferredBeamCandidate> {};
    auto parallel_chunk_tasks = std::vector<ParallelBeamTaskInput> {};
    auto parallel_beam_pool = std::unique_ptr<BS::thread_pool> {};
    auto in_flight_chunks = std::deque<InFlightChunk> {};
    auto ready_chunks = std::map<uint64_t, EvaluatedChunk> {};
    const auto max_in_flight_chunks = std::max<size_t>(1, 2 * static_cast<size_t>(parallel_beam_num_threads));
    auto parallel_ready_queue_high_water = size_t(0);
    auto parallel_in_flight_chunks_high_water = size_t(0);
    auto parallel_consumer_stall_time = std::chrono::nanoseconds::zero();
    auto parallel_producer_stall_time = std::chrono::nanoseconds::zero();
    current_layer.push_back(start_state);

    if (use_parallel_beam)
    {
        parallel_beam_pool = std::make_unique<BS::thread_pool>(parallel_beam_num_threads);
        parallel_chunk_tasks.reserve(parallel_beam_chunk_size);
    }

    const auto finalize_parallel_pipeline = [&]()
    {
        if (use_parallel_beam)
        {
            event_handler->on_finish_parallel_beam_pipeline(parallel_ready_queue_high_water,
                                                           parallel_in_flight_chunks_high_water,
                                                           parallel_consumer_stall_time,
                                                           parallel_producer_stall_time);
        }
    };

    while (!current_layer.empty())
    {
        beam_candidates.clear();
        beam_candidates.reserve(std::min<size_t>(beam_width, current_layer.size()));
        deferred_beam_candidates.clear();
        deferred_beam_candidates.reserve(std::min<size_t>(beam_width, current_layer.size()));
        auto generation_sequence = uint64_t(0);
        auto candidate_generation_sequence = uint64_t(0);
        auto next_chunk_id_to_submit = uint64_t(0);
        auto next_chunk_id_to_merge = uint64_t(0);

        const auto submit_parallel_chunk = [&]()
        {
            if (!use_parallel_beam || parallel_chunk_tasks.empty())
            {
                return;
            }

            auto chunk_tasks = std::vector<ParallelBeamTaskInput> {};
            chunk_tasks.swap(parallel_chunk_tasks);
            parallel_chunk_tasks.reserve(parallel_beam_chunk_size);

            const auto chunk_id = next_chunk_id_to_submit++;
            const auto first_generation_sequence = chunk_tasks.front().generation_sequence;

            in_flight_chunks.push_back(InFlightChunk {
                chunk_id,
                parallel_beam_pool->submit_task([&state_repository, &layer_ordering_strategy, chunk_id, first_generation_sequence, chunk_tasks = std::move(chunk_tasks)]()
                                                {
                                                    thread_local auto scratch =
                                                        std::make_unique<StateRepositoryImpl::StagedSuccessorScratch>();

                                                    auto evaluated_chunk =
                                                        EvaluatedChunk { chunk_id, first_generation_sequence, {}, std::chrono::nanoseconds::zero() };
                                                    evaluated_chunk.candidates.reserve(chunk_tasks.size());

                                                    const auto worker_compute_start = std::chrono::steady_clock::now();
                                                    for (const auto& task : chunk_tasks)
                                                    {
                                                        auto successor_state = state_repository.compute_staged_successor_state(*task.parent_state,
                                                                                                                              task.action,
                                                                                                                              task.parent_metric_value,
                                                                                                                              *scratch);
                                                        const auto action_cost = successor_state.metric_value - task.parent_metric_value;
                                                        const auto score = layer_ordering_strategy->supports_staged_scoring() ?
                                                                               layer_ordering_strategy->score_staged_state(
                                                                                   successor_state.fluent_atoms,
                                                                                   successor_state.derived_atoms,
                                                                                   successor_state.fluent_numeric_variables,
                                                                                   task.successor_g_value) :
                                                                               ContinuousCost(0);

                                                        evaluated_chunk.candidates.push_back(
                                                            ParallelBeamEvaluatedCandidate { task, std::move(successor_state), action_cost, score });
                                                    }
                                                    evaluated_chunk.worker_compute_time =
                                                        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - worker_compute_start);
                                                    return evaluated_chunk;
                                                })
            });
            parallel_in_flight_chunks_high_water = std::max(parallel_in_flight_chunks_high_water, in_flight_chunks.size());
        };

        const auto collect_ready_chunks = [&](bool wait_for_next_chunk) -> std::chrono::nanoseconds
        {
            if (in_flight_chunks.empty())
            {
                return std::chrono::nanoseconds::zero();
            }

            auto waited_time = std::chrono::nanoseconds::zero();
            if (wait_for_next_chunk)
            {
                const auto wait_start = std::chrono::steady_clock::now();
                in_flight_chunks.front().future.wait();
                waited_time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - wait_start);
            }

            for (auto it = in_flight_chunks.begin(); it != in_flight_chunks.end();)
            {
                if (it->future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
                {
                    auto evaluated_chunk = it->future.get();
                    ready_chunks.emplace(it->chunk_id, std::move(evaluated_chunk));
                    parallel_ready_queue_high_water = std::max(parallel_ready_queue_high_water, ready_chunks.size());
                    it = in_flight_chunks.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            return waited_time;
        };

        const auto process_ready_chunk = [&](EvaluatedChunk& evaluated_chunk) -> bool
        {
            const auto merge_start = std::chrono::steady_clock::now();
            auto main_thread_intern_time = std::chrono::nanoseconds::zero();
            auto fluent_slot_time = std::chrono::nanoseconds::zero();
            auto numeric_slot_time = std::chrono::nanoseconds::zero();
            auto derived_slot_time = std::chrono::nanoseconds::zero();
            auto state_lookup_time = std::chrono::nanoseconds::zero();
            auto reached_atom_update_time = std::chrono::nanoseconds::zero();

            for (auto& evaluated_candidate : evaluated_chunk.candidates)
            {
                auto intern_timings = StateRepositoryImpl::StagedSuccessorInternTimings {};
                const auto intern_start = std::chrono::steady_clock::now();
                const auto successor_handle = state_repository.get_or_create_staged_successor_handle(evaluated_candidate.successor_state, &intern_timings);
                main_thread_intern_time += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - intern_start);
                fluent_slot_time += intern_timings.fluent_slot_time;
                numeric_slot_time += intern_timings.numeric_slot_time;
                derived_slot_time += intern_timings.derived_slot_time;
                state_lookup_time += intern_timings.state_lookup_time;
                reached_atom_update_time += intern_timings.reached_atom_update_time;

                const auto is_new_successor = generated_state_indices.insert(successor_handle.state_index).second;

                if (use_staged_parallel_fast_path)
                {
                    event_handler->on_generate_state_without_payload();
                    const auto pruned_for_selection =
                        pruning_strategy->test_prune_staged_successor_state_for_beam_selection(*evaluated_candidate.task.parent_state,
                                                                                               evaluated_candidate.successor_state.fluent_atoms,
                                                                                               evaluated_candidate.successor_state.derived_atoms,
                                                                                               evaluated_candidate.successor_state.fluent_numeric_variables,
                                                                                               evaluated_candidate.successor_state.fluent_atom_indices,
                                                                                               is_new_successor,
                                                                                               beam_novelty_mode);

                    if (!is_new_successor || pruned_for_selection)
                    {
                        event_handler->on_generate_state_not_in_search_tree_without_payload();
                        continue;
                    }

                    auto candidate = DeferredBeamCandidate { evaluated_candidate.task.parent_state,
                                                            evaluated_candidate.task.action,
                                                            evaluated_candidate.action_cost,
                                                            std::move(evaluated_candidate.successor_state),
                                                            successor_handle,
                                                            evaluated_candidate.task.successor_g_value,
                                                            evaluated_candidate.score,
                                                            candidate_generation_sequence++,
                                                            options.randomize_equal_score_ties ? tie_break_rng() : uint64_t(0),
                                                            is_new_successor };

                    const auto reject_candidate = [&](const auto&) { event_handler->on_generate_state_not_in_search_tree_without_payload(); };
                    if (use_small_beam)
                    {
                        add_candidate_to_small_beam(deferred_beam_candidates,
                                                    std::move(candidate),
                                                    beam_width,
                                                    ranking,
                                                    reject_candidate);
                    }
                    else
                    {
                        add_candidate_to_heap_beam(deferred_beam_candidates,
                                                   std::move(candidate),
                                                   beam_width,
                                                   ranking,
                                                   deferred_heap_compare,
                                                   reject_candidate);
                    }
                }
                else
                {
                    auto successor_state = state_repository.materialize_staged_successor_state(evaluated_candidate.successor_state, successor_handle);
                    event_handler->on_generate_state(*evaluated_candidate.task.parent_state,
                                                     evaluated_candidate.task.action,
                                                     evaluated_candidate.action_cost,
                                                     successor_state);
                    const auto pruned_for_selection =
                        pruning_strategy->test_prune_successor_state_for_beam_selection(*evaluated_candidate.task.parent_state,
                                                                                        successor_state,
                                                                                        is_new_successor,
                                                                                        beam_novelty_mode);

                    if (!is_new_successor || pruned_for_selection)
                    {
                        event_handler->on_generate_state_not_in_search_tree(*evaluated_candidate.task.parent_state,
                                                                            evaluated_candidate.task.action,
                                                                            evaluated_candidate.action_cost,
                                                                            successor_state);
                        continue;
                    }

                    auto candidate = BeamCandidate { evaluated_candidate.task.parent_state,
                                                    evaluated_candidate.task.action,
                                                    evaluated_candidate.action_cost,
                                                    successor_state,
                                                    evaluated_candidate.task.successor_g_value,
                                                    layer_ordering_strategy->supports_staged_scoring() ?
                                                        evaluated_candidate.score :
                                                        layer_ordering_strategy->score_state(successor_state, evaluated_candidate.task.successor_g_value),
                                                    candidate_generation_sequence++,
                                                    options.randomize_equal_score_ties ? tie_break_rng() : uint64_t(0),
                                                    is_new_successor };

                    const auto reject_candidate = [&](const BeamCandidate& candidate) { reject_beam_candidate(candidate, event_handler); };
                    if (use_small_beam)
                    {
                        add_candidate_to_small_beam(beam_candidates, std::move(candidate), beam_width, ranking, reject_candidate);
                    }
                    else
                    {
                        add_candidate_to_heap_beam(beam_candidates, std::move(candidate), beam_width, ranking, heap_compare, reject_candidate);
                    }
                }

                if (generated_state_indices.size() >= options.max_num_states)
                {
                    result.status = SearchStatus::OUT_OF_STATES;

                    const auto merge_end = std::chrono::steady_clock::now();
                    event_handler->on_finish_parallel_beam_chunk(evaluated_chunk.candidates.size(),
                                                                 evaluated_chunk.worker_compute_time,
                                                                 std::chrono::duration_cast<std::chrono::nanoseconds>(merge_end - merge_start),
                                                                 main_thread_intern_time,
                                                                 fluent_slot_time,
                                                                 numeric_slot_time,
                                                                 derived_slot_time,
                                                                 state_lookup_time,
                                                                 reached_atom_update_time);
                    return false;
                }
            }

            const auto merge_end = std::chrono::steady_clock::now();
            event_handler->on_finish_parallel_beam_chunk(evaluated_chunk.candidates.size(),
                                                         evaluated_chunk.worker_compute_time,
                                                         std::chrono::duration_cast<std::chrono::nanoseconds>(merge_end - merge_start),
                                                         main_thread_intern_time,
                                                         fluent_slot_time,
                                                         numeric_slot_time,
                                                         derived_slot_time,
                                                         state_lookup_time,
                                                         reached_atom_update_time);
            return true;
        };

        const auto drain_ready_chunks = [&]() -> bool
        {
            while (true)
            {
                auto it = ready_chunks.find(next_chunk_id_to_merge);
                if (it == ready_chunks.end())
                {
                    break;
                }

                auto evaluated_chunk = std::move(it->second);
                ready_chunks.erase(it);
                if (!process_ready_chunk(evaluated_chunk))
                {
                    return false;
                }
                ++next_chunk_id_to_merge;
            }

            return true;
        };

        const auto merge_fused_relaxed_candidates = [&](std::vector<ParallelRelaxedBeamSuccessorCandidate>& merged_candidates,
                                                        size_t num_scored_candidates,
                                                        std::chrono::nanoseconds total_worker_compute_time) -> bool
        {
            sort_beam_candidates(merged_candidates, ranking);

            const auto merge_start = std::chrono::steady_clock::now();
            auto main_thread_intern_time = std::chrono::nanoseconds::zero();
            auto fluent_slot_time = std::chrono::nanoseconds::zero();
            auto numeric_slot_time = std::chrono::nanoseconds::zero();
            auto derived_slot_time = std::chrono::nanoseconds::zero();
            auto state_lookup_time = std::chrono::nanoseconds::zero();
            auto reached_atom_update_time = std::chrono::nanoseconds::zero();

            for (auto& candidate : merged_candidates)
            {
                event_handler->on_generate_state_without_payload();

                auto staged_successor = StateRepositoryImpl::StagedSuccessorState {};
                staged_successor.fluent_atoms = std::move(candidate.fluent_atoms);
                staged_successor.derived_atoms = std::move(candidate.derived_atoms);
                staged_successor.fluent_atom_indices = std::move(candidate.fluent_atom_indices);
                staged_successor.derived_atom_indices = std::move(candidate.derived_atom_indices);
                staged_successor.fluent_numeric_variables = std::move(candidate.fluent_numeric_variables);
                staged_successor.metric_value = candidate.successor_metric_value;

                auto intern_timings = StateRepositoryImpl::StagedSuccessorInternTimings {};
                const auto intern_start = std::chrono::steady_clock::now();
                const auto successor_handle = state_repository.get_or_create_staged_successor_handle(staged_successor, &intern_timings);
                main_thread_intern_time += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - intern_start);
                fluent_slot_time += intern_timings.fluent_slot_time;
                numeric_slot_time += intern_timings.numeric_slot_time;
                derived_slot_time += intern_timings.derived_slot_time;
                state_lookup_time += intern_timings.state_lookup_time;
                reached_atom_update_time += intern_timings.reached_atom_update_time;

                if (!generated_state_indices.insert(successor_handle.state_index).second)
                {
                    event_handler->on_generate_state_not_in_search_tree_without_payload();
                    continue;
                }

                auto binding = formalism::ObjectList(candidate.binding_object_indices.size());
                const auto& problem_objects = problem_handle->get_problem_and_domain_objects();
                for (size_t i = 0; i < candidate.binding_object_indices.size(); ++i)
                {
                    binding[i] = problem_objects[candidate.binding_object_indices[i]];
                }
                const auto ground_action = problem_handle->ground(candidate.action_schema, binding);
                deferred_beam_candidates.push_back(DeferredBeamCandidate { candidate.parent_state,
                                                                           ground_action,
                                                                           candidate.action_cost,
                                                                           std::move(staged_successor),
                                                                           successor_handle,
                                                                           candidate.successor_g_value,
                                                                           candidate.score,
                                                                           candidate.generation_sequence,
                                                                           candidate.tie_token,
                                                                           true });

                if (generated_state_indices.size() >= options.max_num_states)
                {
                    result.status = SearchStatus::OUT_OF_STATES;

                    const auto merge_end = std::chrono::steady_clock::now();
                    event_handler->on_finish_parallel_beam_chunk(num_scored_candidates,
                                                                 total_worker_compute_time,
                                                                 std::chrono::duration_cast<std::chrono::nanoseconds>(merge_end - merge_start),
                                                                 main_thread_intern_time,
                                                                 fluent_slot_time,
                                                                 numeric_slot_time,
                                                                 derived_slot_time,
                                                                 state_lookup_time,
                                                                 reached_atom_update_time);
                    return false;
                }

                if (deferred_beam_candidates.size() >= beam_width)
                {
                    break;
                }
            }

            const auto merge_end = std::chrono::steady_clock::now();
            event_handler->on_finish_parallel_beam_chunk(num_scored_candidates,
                                                         total_worker_compute_time,
                                                         std::chrono::duration_cast<std::chrono::nanoseconds>(merge_end - merge_start),
                                                         main_thread_intern_time,
                                                         fluent_slot_time,
                                                         numeric_slot_time,
                                                         derived_slot_time,
                                                         state_lookup_time,
                                                         reached_atom_update_time);
            return true;
        };

        const auto process_relaxed_parallel_layer = [&]() -> bool
        {
            if (!use_relaxed_staged_parallel_fast_path)
            {
                return true;
            }

            const auto total_task_count = parallel_chunk_tasks.size();
            if (total_task_count == 0)
            {
                return true;
            }

            const auto num_partitions =
                std::min<size_t>(parallel_beam_num_threads, std::max<size_t>(1, (total_task_count + parallel_beam_chunk_size - 1) / parallel_beam_chunk_size));
            auto partition_futures = std::vector<std::future<RelaxedPartitionResult>> {};
            partition_futures.reserve(num_partitions);

            const auto relaxed_heap_compare = BeamHeapCompare<RelaxedDeferredBeamCandidate> { ranking };

            auto begin_index = size_t(0);
            const auto base_partition_size = total_task_count / num_partitions;
            const auto num_larger_partitions = total_task_count % num_partitions;

            for (size_t partition_index = 0; partition_index < num_partitions; ++partition_index)
            {
                const auto partition_size = base_partition_size + (partition_index < num_larger_partitions ? 1u : 0u);
                const auto partition_begin = begin_index;
                const auto partition_end = partition_begin + partition_size;
                begin_index = partition_end;

                partition_futures.push_back(
                    parallel_beam_pool->submit_task([&state_repository,
                                                     &layer_ordering_strategy,
                                                     &pruning_strategy,
                                                     &parallel_chunk_tasks,
                                                     beam_novelty_mode,
                                                     use_small_beam,
                                                     beam_width,
                                                     ranking,
                                                     relaxed_heap_compare,
                                                     partition_begin,
                                                     partition_end]()
                                                    {
                                                        thread_local auto scratch =
                                                            std::make_unique<StateRepositoryImpl::StagedSuccessorScratch>();

                                                        auto result =
                                                            RelaxedPartitionResult { {}, std::chrono::nanoseconds::zero(), partition_end - partition_begin };
                                                        result.candidates.reserve(std::min<size_t>(beam_width, partition_end - partition_begin));

                                                        const auto worker_compute_start = std::chrono::steady_clock::now();
                                                        for (auto task_index = partition_begin; task_index < partition_end; ++task_index)
                                                        {
                                                            const auto& task = parallel_chunk_tasks[task_index];
                                                            auto successor_state = state_repository.compute_staged_successor_state(*task.parent_state,
                                                                                                                                  task.action,
                                                                                                                                  task.parent_metric_value,
                                                                                                                                  *scratch);
                                                            if (pruning_strategy->test_prune_staged_successor_state_for_relaxed_beam_selection(
                                                                    *task.parent_state,
                                                                    successor_state.fluent_atoms,
                                                                    successor_state.derived_atoms,
                                                                    successor_state.fluent_numeric_variables,
                                                                    successor_state.fluent_atom_indices,
                                                                    beam_novelty_mode))
                                                            {
                                                                continue;
                                                            }

                                                            auto candidate = RelaxedDeferredBeamCandidate { task.parent_state,
                                                                                                            task.action,
                                                                                                            successor_state.metric_value - task.parent_metric_value,
                                                                                                            std::move(successor_state),
                                                                                                            task.successor_g_value,
                                                                                                            layer_ordering_strategy->score_staged_state(
                                                                                                                successor_state.fluent_atoms,
                                                                                                                successor_state.derived_atoms,
                                                                                                                successor_state.fluent_numeric_variables,
                                                                                                                task.successor_g_value),
                                                                                                            task.generation_sequence,
                                                                                                            task.tie_token };

                                                            const auto reject_candidate = [](const auto&) {};
                                                            if (use_small_beam)
                                                            {
                                                                add_candidate_to_small_beam(result.candidates,
                                                                                            std::move(candidate),
                                                                                            beam_width,
                                                                                            ranking,
                                                                                            reject_candidate);
                                                            }
                                                            else
                                                            {
                                                                add_candidate_to_heap_beam(result.candidates,
                                                                                           std::move(candidate),
                                                                                           beam_width,
                                                                                           ranking,
                                                                                           relaxed_heap_compare,
                                                                                           reject_candidate);
                                                            }
                                                        }

                                                        result.worker_compute_time =
                                                            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()
                                                                                                                 - worker_compute_start);
                                                        return result;
                                                    }));
            }

            auto merged_candidates = std::vector<RelaxedDeferredBeamCandidate> {};
            merged_candidates.reserve(std::min<size_t>(num_partitions * beam_width, total_task_count));
            auto total_worker_compute_time = std::chrono::nanoseconds::zero();

            for (auto& future : partition_futures)
            {
                auto partition_result = future.get();
                total_worker_compute_time += partition_result.worker_compute_time;
                for (auto& candidate : partition_result.candidates)
                {
                    merged_candidates.push_back(std::move(candidate));
                }
            }

            sort_beam_candidates(merged_candidates, ranking);

            const auto merge_start = std::chrono::steady_clock::now();
            auto main_thread_intern_time = std::chrono::nanoseconds::zero();
            auto fluent_slot_time = std::chrono::nanoseconds::zero();
            auto numeric_slot_time = std::chrono::nanoseconds::zero();
            auto derived_slot_time = std::chrono::nanoseconds::zero();
            auto state_lookup_time = std::chrono::nanoseconds::zero();
            auto reached_atom_update_time = std::chrono::nanoseconds::zero();

            for (auto& candidate : merged_candidates)
            {
                event_handler->on_generate_state_without_payload();

                auto intern_timings = StateRepositoryImpl::StagedSuccessorInternTimings {};
                const auto intern_start = std::chrono::steady_clock::now();
                const auto successor_handle = state_repository.get_or_create_staged_successor_handle(candidate.successor_state, &intern_timings);
                main_thread_intern_time += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - intern_start);
                fluent_slot_time += intern_timings.fluent_slot_time;
                numeric_slot_time += intern_timings.numeric_slot_time;
                derived_slot_time += intern_timings.derived_slot_time;
                state_lookup_time += intern_timings.state_lookup_time;
                reached_atom_update_time += intern_timings.reached_atom_update_time;

                if (!generated_state_indices.insert(successor_handle.state_index).second)
                {
                    event_handler->on_generate_state_not_in_search_tree_without_payload();
                    continue;
                }

                deferred_beam_candidates.push_back(DeferredBeamCandidate { candidate.parent_state,
                                                                           candidate.action,
                                                                           candidate.action_cost,
                                                                           std::move(candidate.successor_state),
                                                                           successor_handle,
                                                                           candidate.successor_g_value,
                                                                           candidate.score,
                                                                           candidate.generation_sequence,
                                                                           candidate.tie_token,
                                                                           true });

                if (generated_state_indices.size() >= options.max_num_states)
                {
                    result.status = SearchStatus::OUT_OF_STATES;

                    const auto merge_end = std::chrono::steady_clock::now();
                    event_handler->on_finish_parallel_beam_chunk(total_task_count,
                                                                 total_worker_compute_time,
                                                                 std::chrono::duration_cast<std::chrono::nanoseconds>(merge_end - merge_start),
                                                                 main_thread_intern_time,
                                                                 fluent_slot_time,
                                                                 numeric_slot_time,
                                                                 derived_slot_time,
                                                                 state_lookup_time,
                                                                 reached_atom_update_time);
                    parallel_chunk_tasks.clear();
                    return false;
                }

                if (deferred_beam_candidates.size() >= beam_width)
                {
                    break;
                }
            }

            const auto merge_end = std::chrono::steady_clock::now();
            event_handler->on_finish_parallel_beam_chunk(total_task_count,
                                                         total_worker_compute_time,
                                                         std::chrono::duration_cast<std::chrono::nanoseconds>(merge_end - merge_start),
                                                         main_thread_intern_time,
                                                         fluent_slot_time,
                                                         numeric_slot_time,
                                                         derived_slot_time,
                                                         state_lookup_time,
                                                         reached_atom_update_time);
            parallel_chunk_tasks.clear();
            return true;
        };

        for (const auto& state : current_layer)
        {
            if (stopwatch.has_finished())
            {
                result.status = SearchStatus::OUT_OF_TIME;
                finalize_parallel_pipeline();
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

                    finalize_parallel_pipeline();
                    return result;
                }
            }

            event_handler->on_expand_state(state);
            search_node.status = SearchNodeStatus::CLOSED;

            if (use_max_depth && (search_node.g_value >= max_depth))
            {
                continue;
            }

            if (use_fused_relaxed_parallel_successor_generation)
            {
                auto generation_result = applicable_action_generator.create_relaxed_parallel_beam_successor_candidates(state,
                                                                                                                      static_cast<ContinuousCost>(search_node.g_value),
                                                                                                                      search_node.g_value + 1,
                                                                                                                      *parallel_beam_pool,
                                                                                                                      state_repository,
                                                                                                                      pruning_strategy,
                                                                                                                      beam_novelty_mode,
                                                                                                                      layer_ordering_strategy,
                                                                                                                      beam_width,
                                                                                                                      options.randomize_equal_score_ties,
                                                                                                                      options.equal_score_tie_seed);
                if (!merge_fused_relaxed_candidates(generation_result.candidates,
                                                    generation_result.num_scored_candidates,
                                                    generation_result.worker_compute_time))
                {
                    finalize_parallel_pipeline();
                    return result;
                }
                continue;
            }

            const auto handle_action = [&](const auto& action) -> bool
            {
                if (use_relaxed_staged_parallel_fast_path)
                {
                    parallel_chunk_tasks.push_back(ParallelBeamTaskInput { &state,
                                                                          action,
                                                                          static_cast<ContinuousCost>(search_node.g_value),
                                                                          search_node.g_value + 1,
                                                                          generation_sequence++,
                                                                          options.randomize_equal_score_ties ? tie_break_rng() : uint64_t(0) });
                    return true;
                }

                if (!use_parallel_beam)
                {
                    const auto [successor_state, successor_state_metric_value] =
                        state_repository.get_or_create_successor_state(state, action, search_node.g_value);
                    auto action_cost = successor_state_metric_value - search_node.g_value;
                    const auto successor_g_value = search_node.g_value + 1;
                    const auto is_new_successor = generated_state_indices.insert(successor_state.get_index()).second;

                    event_handler->on_generate_state(state, action, action_cost, successor_state);
                    if (pruning_strategy->test_prune_successor_state_for_beam_selection(state, successor_state, is_new_successor, beam_novelty_mode))
                    {
                        event_handler->on_generate_state_not_in_search_tree(state, action, action_cost, successor_state);
                        return true;
                    }

                    auto candidate = BeamCandidate { &state,
                                                    action,
                                                    action_cost,
                                                    successor_state,
                                                    successor_g_value,
                                                    layer_ordering_strategy->score_state(successor_state, successor_g_value),
                                                    generation_sequence++,
                                                    options.randomize_equal_score_ties ? tie_break_rng() : uint64_t(0),
                                                    is_new_successor };

                    const auto reject_candidate = [&](const BeamCandidate& candidate) { reject_beam_candidate(candidate, event_handler); };
                    if (use_small_beam)
                    {
                        add_candidate_to_small_beam(beam_candidates, std::move(candidate), beam_width, ranking, reject_candidate);
                    }
                    else
                    {
                        add_candidate_to_heap_beam(beam_candidates, std::move(candidate), beam_width, ranking, heap_compare, reject_candidate);
                    }

                    if (generated_state_indices.size() >= options.max_num_states)
                    {
                        result.status = SearchStatus::OUT_OF_STATES;
                        return false;
                    }

                    return true;
                }

                parallel_chunk_tasks.push_back(ParallelBeamTaskInput { &state,
                                                                      action,
                                                                      static_cast<ContinuousCost>(search_node.g_value),
                                                                      search_node.g_value + 1,
                                                                      generation_sequence++,
                                                                      uint64_t(0) });
                if (parallel_chunk_tasks.size() == parallel_beam_chunk_size)
                {
                    submit_parallel_chunk();
                    const auto reached_chunk_window_limit = [&]() -> bool { return in_flight_chunks.size() >= max_in_flight_chunks; };

                    if (reached_chunk_window_limit())
                    {
                        collect_ready_chunks(false);

                        while (reached_chunk_window_limit())
                        {
                            parallel_producer_stall_time += collect_ready_chunks(true);
                            if (!drain_ready_chunks())
                            {
                                return false;
                            }
                        }

                        if (!drain_ready_chunks())
                        {
                            return false;
                        }
                    }
                }
                return true;
            };

            if (use_parallel_action_generation)
            {
                for (const auto& action : applicable_action_generator.create_applicable_action_list_parallel(state, *parallel_beam_pool))
                {
                    if (!handle_action(action))
                    {
                        finalize_parallel_pipeline();
                        return result;
                    }
                }
            }
            else
            {
                for (const auto& action : applicable_action_generator.create_applicable_action_generator(state))
                {
                    if (!handle_action(action))
                    {
                        finalize_parallel_pipeline();
                        return result;
                    }
                }
            }
        }

        if (use_relaxed_staged_parallel_fast_path)
        {
            if (!process_relaxed_parallel_layer())
            {
                finalize_parallel_pipeline();
                return result;
            }
        }
        else if (use_parallel_beam)
        {
            submit_parallel_chunk();
            collect_ready_chunks(false);
            if (!drain_ready_chunks())
            {
                finalize_parallel_pipeline();
                return result;
            }

            const auto has_pending_parallel_chunks = [&]() -> bool
            {
                return !in_flight_chunks.empty() || !ready_chunks.empty();
            };

            while (has_pending_parallel_chunks())
            {
                if (!drain_ready_chunks())
                {
                    finalize_parallel_pipeline();
                    return result;
                }

                const auto should_wait_for_next_chunk = [&]() -> bool
                {
                    return ready_chunks.find(next_chunk_id_to_merge) == ready_chunks.end() && !in_flight_chunks.empty();
                };

                if (should_wait_for_next_chunk())
                {
                    parallel_consumer_stall_time += collect_ready_chunks(true);
                }
                else
                {
                    collect_ready_chunks(false);
                }
            }

            if (!drain_ready_chunks())
            {
                finalize_parallel_pipeline();
                return result;
            }
        }

        if (!use_small_beam)
        {
            if (use_staged_parallel_fast_path)
            {
                sort_beam_candidates(deferred_beam_candidates, ranking);
            }
            else
            {
                sort_beam_candidates(beam_candidates, ranking);
            }
        }

        if (use_staged_parallel_fast_path)
        {
            finalize_deferred_beam_layer(
                event_handler, pruning_strategy, beam_novelty_mode, state_repository, search_nodes, deferred_beam_candidates, next_layer);
            deferred_beam_candidates.clear();
        }
        else
        {
            finalize_beam_layer(event_handler, pruning_strategy, beam_novelty_mode, search_nodes, beam_candidates, next_layer);
            beam_candidates.clear();
        }

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
    finalize_parallel_pipeline();
    return result;
}
}
