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

#ifndef MIMIR_SRC_SEARCH_ALGORITHMS_BRFS_TRANSITION_ORDERED_LAYER_IMPL_HPP_
#define MIMIR_SRC_SEARCH_ALGORITHMS_BRFS_TRANSITION_ORDERED_LAYER_IMPL_HPP_

#include "transition_ordered_layer.hpp"

#include "mimir/formalism/problem.hpp"
#include "mimir/search/algorithms/brfs/event_handlers/interface.hpp"
#include "mimir/search/algorithms/strategies/goal_strategy.hpp"
#include "mimir/search/algorithms/strategies/pruning_strategy.hpp"
#include "mimir/search/algorithms/strategies/transition_ordering_strategy.hpp"
#include "mimir/search/applicable_action_generators/interface.hpp"
#include "mimir/search/axiom_evaluators/interface.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/search_space.hpp"
#include "mimir/search/state_repository.hpp"

#include <algorithm>
#include <utility>
#include <vector>

// NOTE: this header is only ever included by transition_ordered_layer.cpp (which then explicitly
// instantiates the template below), so it deliberately does not open a translation-unit-wide
// `using namespace mimir::formalism;` the way brfs.cpp/ordered_layer.cpp do -- symbols are qualified
// with `formalism::` throughout, matching the house style of the other headers in this directory
// (internal.hpp).

namespace mimir::search::brfs
{

/// Per-layer loop:
///   1. Generation: for each parent in `current_layer`, run the same per-state bookkeeping as
///      `find_solution_with_ordered_layer` (stopwatch/goal-check/expand-event/
///      consume_skip_state_expansion/max_depth), then for each applicable action materialize the
///      successor via `state_repository.get_or_create_successor_state(...)` ONLY -- no search-node
///      lookup/mutation happens here -- and store `ordering.score(parent, action, successor, depth+1)`
///      into a candidate record.
///   2. Sort: `std::stable_sort` the whole layer's candidates by `ordering.prefer(a.score, b.score)`.
///   3. Admission: in that sorted order, call `get_or_create_search_node(successor)` for the first
///      time to determine `is_new_successor`, then `pruning_strategy->test_prune_successor_state(...)`.
///
/// Why `is_new_successor` is resolved lazily at admission time (not frozen during generation): today,
/// "is new" means "wins the race to be first-admitted" -- a pruned candidate never flips its target
/// state's search-node status, so a later same-layer candidate aimed at the same state can still see
/// NEW. Freezing this at generation time would let raw enumeration order decide every contested claim
/// regardless of score, defeating the point of scoring at all. Resolving it lazily, in sorted order, is
/// what lets a better-scored transition win a contested novelty witness *and* a contested successor
/// state over a worse-scored one. This composes for free with `ArityKNoveltyPruningStrategyImpl`'s (and
/// `AbstractedNoveltyPruningStrategyImpl`'s) `optimize_root_depth_one_continuation` handling: both call
/// `pruning_strategy->test_prune_successor_state(...)` polymorphically without knowing the concrete
/// strategy, exactly like the existing queued path, so a landmark-preferred depth-1 transition
/// naturally claims the shared novelty table first and keeps expansion rights.
template<TransitionOrderingStrategy Ordering>
SearchResult find_solution_with_transition_ordering(const SearchContext& context,
                                                     const Options& options,
                                                     const Ordering& ordering,
                                                     const State& start_state,
                                                     ContinuousCost start_g_value,
                                                     const EventHandler& event_handler,
                                                     const GoalStrategy& goal_strategy,
                                                     const PruningStrategy& pruning_strategy,
                                                     SearchNodeVector& search_nodes,
                                                     DiscreteCost g_value,
                                                     StopWatch& stopwatch,
                                                     SearchEndGuard& end_guard)
{
    auto& applicable_action_generator = *context->get_applicable_action_generator();
    auto& state_repository = *context->get_state_repository();

    auto result = SearchResult();
    const auto max_depth = options.max_depth;
    const auto use_max_depth = (max_depth < std::numeric_limits<uint32_t>::max());

    const auto emit_novel_witness_events = event_handler->supports_novel_witness_events() && pruning_strategy->supports_transition_novel_witness_query();
    auto novel_witness_atom_indices = iw::AtomIndexList {};

    using Score = decltype(std::declval<const Ordering&>().score(std::declval<const State&>(),
                                                                  std::declval<formalism::GroundAction>(),
                                                                  std::declval<const State&>(),
                                                                  std::declval<DiscreteCost>()));

    struct Candidate
    {
        State parent;
        formalism::GroundAction action;
        ContinuousCost action_cost;
        State successor;
        size_t generation_sequence;
        Score score;
    };

    auto current_layer = StateList {};
    auto next_layer = StateList {};
    auto candidates = std::vector<Candidate> {};
    current_layer.push_back(start_state);

    while (!current_layer.empty())
    {
        candidates.clear();
        auto generation_sequence = size_t(0);

        /**
         * Generation: materialize every candidate transition of this layer. No search-node lookup or
         * mutation happens for successors here -- only for the parent, to mark it CLOSED and read its
         * g_value for `action_cost` bookkeeping.
         */

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
                    end_guard.finish();

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

            for (const auto& action : applicable_action_generator.create_applicable_action_generator(state))
            {
                const auto [successor_state, successor_state_metric_value] =
                    state_repository.get_or_create_successor_state(state, action, search_node.g_value);
                const auto action_cost = successor_state_metric_value - search_node.g_value;

                auto score = ordering.score(state, action, successor_state, search_node.g_value + 1);

                candidates.push_back(Candidate { state, action, action_cost, successor_state, generation_sequence, std::move(score) });
                ++generation_sequence;
            }
        }

        /**
         * Sort: order the whole layer's candidates by preference. std::stable_sort preserves relative
         * generation order among equally-scored candidates, so no explicit tie-break field is needed.
         */

        std::stable_sort(candidates.begin(),
                         candidates.end(),
                         [&ordering](const Candidate& lhs, const Candidate& rhs) { return ordering.prefer(lhs.score, rhs.score); });

        /**
         * Admission: in sorted order, resolve is_new_successor lazily and run novelty pruning. The
         * first (i.e. best-scored) candidate to reach a given successor state wins any contested claim.
         */

        const auto next_layer_g_value = g_value + 1;

        for (const auto& candidate : candidates)
        {
            const auto& parent_state = candidate.parent;
            const auto& action = candidate.action;
            const auto& successor_state = candidate.successor;
            const auto action_cost = candidate.action_cost;

            auto& successor_search_node = get_or_create_search_node(successor_state.get_index(), search_nodes);
            const auto is_new_successor = (successor_search_node.status == SearchNodeStatus::NEW);

            if (emit_novel_witness_events)
            {
                pruning_strategy->compute_transition_novel_fluent_atom_indices_read_only(parent_state, successor_state, novel_witness_atom_indices);
                event_handler->on_generate_state_with_novel_witness(parent_state, action, action_cost, successor_state, novel_witness_atom_indices);
            }
            event_handler->on_generate_state(parent_state, action, action_cost, successor_state);

            if (pruning_strategy->test_prune_successor_state(parent_state, successor_state, is_new_successor))
            {
                event_handler->on_generate_state_not_in_search_tree(parent_state, action, action_cost, successor_state);
                continue;
            }
            event_handler->on_generate_state_in_search_tree(parent_state, action, action_cost, successor_state);

            successor_search_node.status = SearchNodeStatus::OPEN;
            successor_search_node.parent_state = parent_state.get_index();
            successor_search_node.incoming_action = action->get_index();
            successor_search_node.g_value = next_layer_g_value;

            next_layer.emplace_back(successor_state);

            if (search_nodes.size() >= options.max_num_states)
            {
                result.status = SearchStatus::OUT_OF_STATES;
                return result;
            }
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

    end_guard.finish();
    event_handler->on_exhausted();

    result.status = SearchStatus::EXHAUSTED;
    return result;
}

}

#endif
