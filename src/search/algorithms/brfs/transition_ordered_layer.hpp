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

#ifndef MIMIR_SRC_SEARCH_ALGORITHMS_BRFS_TRANSITION_ORDERED_LAYER_HPP_
#define MIMIR_SRC_SEARCH_ALGORITHMS_BRFS_TRANSITION_ORDERED_LAYER_HPP_

#include "internal.hpp"

#include "mimir/search/algorithms/strategies/transition_ordering_strategy.hpp"

namespace mimir::search::brfs
{

/// @brief Transition-ordered BrFS keeps breadth-first expansion by depth, but -- unlike the queued
/// default path -- it generates *all* candidate transitions of a layer first, scores each with
/// `ordering.score(...)`, sorts the whole layer's candidates by `ordering.prefer(...)`, and only then
/// runs novelty-pruning admission in that sorted order. This lets a better-scored transition win a
/// contested novelty witness or successor state over a worse-scored one, which raw generation order
/// can never do. See `transition_ordered_layer_impl.hpp` for the algorithm and its rationale.
///
/// This function is a genuine template over `Ordering` (constrained by the `TransitionOrderingStrategy`
/// concept from `transition_ordering_strategy.hpp`), not hardcoded to any concrete strategy. In v1, the
/// only concrete type that reaches it is `LandmarkTransitionOrderingStrategy` -- guarded by the
/// `if constexpr (Ordering::requires_deferred_novelty)` dispatch in `brfs.cpp` -- so only that
/// specialization is explicitly instantiated (see `transition_ordered_layer.cpp`). A future second
/// deferred strategy only needs its own explicit instantiation line there; this template itself would
/// not need to change.
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
                                                     SearchEndGuard& end_guard);

// Explicit instantiation is provided by transition_ordered_layer.cpp. Declaring it `extern` here lets
// translation units (namely brfs.cpp) call this function while only including this thin declaration
// header -- they never need to see transition_ordered_layer_impl.hpp's full template body.
extern template SearchResult find_solution_with_transition_ordering<LandmarkTransitionOrderingStrategy>(const SearchContext& context,
                                                                                                         const Options& options,
                                                                                                         const LandmarkTransitionOrderingStrategy& ordering,
                                                                                                         const State& start_state,
                                                                                                         ContinuousCost start_g_value,
                                                                                                         const EventHandler& event_handler,
                                                                                                         const GoalStrategy& goal_strategy,
                                                                                                         const PruningStrategy& pruning_strategy,
                                                                                                         SearchNodeVector& search_nodes,
                                                                                                         DiscreteCost g_value,
                                                                                                         StopWatch& stopwatch,
                                                                                                         SearchEndGuard& end_guard);

}

#endif
