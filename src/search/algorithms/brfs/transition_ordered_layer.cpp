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

#include "transition_ordered_layer_impl.hpp"

namespace mimir::search::brfs
{

// The only concrete deferred-novelty ordering strategy that reaches this function in v1 (see
// find_solution_impl's `if constexpr (Ordering::requires_deferred_novelty)` dispatch in brfs.cpp). A
// future second deferred strategy only needs its own explicit instantiation line here.
template SearchResult find_solution_with_transition_ordering<LandmarkTransitionOrderingStrategy>(const SearchContext& context,
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
