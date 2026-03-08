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

#ifndef MIMIR_SRC_SEARCH_ALGORITHMS_BRFS_INTERNAL_HPP_
#define MIMIR_SRC_SEARCH_ALGORITHMS_BRFS_INTERNAL_HPP_

#include <cassert>
#include <limits>

#include "mimir/common/segmented_vector.hpp"
#include "mimir/common/timers.hpp"
#include "mimir/search/algorithms/brfs.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/search/search_node.hpp"

namespace mimir::search::brfs
{
struct SearchNode
{
    DiscreteCost g_value;
    Index parent_state;
    SearchNodeStatus status;
};

using SearchNodeVector = SegmentedVector<SearchNode>;

inline SearchNode& get_or_create_search_node(size_t state_index, SearchNodeVector& search_nodes)
{
    static constexpr auto default_node = SearchNode { DiscreteCost(0), std::numeric_limits<Index>::max(), SearchNodeStatus::NEW };

    while (state_index >= search_nodes.size())
    {
        search_nodes.push_back(default_node);
    }
    return search_nodes[state_index];
}

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
                                     StopWatch& stopwatch);

/// Ordered-layer BrFS keeps breadth-first expansion by depth, but it may score and reorder
/// states inside the next layer. With `max_next_layer_states`, generation can stop early once
/// the depth-(d+1) buffer is full.
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
                                              StopWatch& stopwatch);
}

#endif
