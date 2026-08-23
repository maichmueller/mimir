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

#include "mimir/common/segmented_vector.hpp"
#include "mimir/common/timers.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/search/algorithms/brfs.hpp"
#include "mimir/search/algorithms/strategies/pruning_strategy.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/search/search_node.hpp"
#include "mimir/search/state_repository.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_map>

namespace mimir::search::brfs
{
struct SearchNode
{
    DiscreteCost g_value;
    Index parent_state;
    Index incoming_action;
    SearchNodeStatus status;
};

static constexpr auto kInvalidGroundActionIndex = std::numeric_limits<Index>::max();

using SearchNodeVector = SegmentedVector<SearchNode>;

inline SearchNode& get_or_create_search_node(size_t state_index, SearchNodeVector& search_nodes)
{
    static constexpr auto default_node = SearchNode { DiscreteCost(0), std::numeric_limits<Index>::max(), kInvalidGroundActionIndex, SearchNodeStatus::NEW };

    while (state_index >= search_nodes.size())
    {
        search_nodes.push_back(default_node);
    }
    return search_nodes[state_index];
}

/// @brief Emits `IEventHandler::on_end_search` exactly once for a search that has started, whichever
/// of BrFS's many terminal paths returns.
///
/// BrFS leaves through a dozen `return result;` statements -- solved, exhausted, out of time, out of
/// states, canceled, unsolvable, initial-state pruning -- and every one of them owes the handler the
/// end-of-search repository counts and the stop time that finalize its statistics. Keeping that
/// obligation in a destructor puts it on the search's scope instead of on whoever writes the next
/// early return.
class SearchEndGuard
{
public:
    SearchEndGuard(EventHandler event_handler, SearchContext context, const SearchNodeVector& search_nodes);
    SearchEndGuard(const SearchEndGuard&) = delete;
    SearchEndGuard& operator=(const SearchEndGuard&) = delete;
    ~SearchEndGuard() noexcept(false);

    /// @brief Emit the event now instead of at scope exit. Paths that report more afterwards --
    /// extracting a plan and emitting `on_solved`, say -- call this so the handler still sees
    /// end-of-search first. Any call after the first is a no-op.
    void finish();

private:
    EventHandler m_event_handler;
    SearchContext m_context;
    const SearchNodeVector* m_search_nodes;
    bool m_finished;
};

class IW1ActionPrecheckController
{
private:
    bool m_enabled;
    bool m_atom_first_mode;
    /// Whether the pruning strategy's precheck also needs the transition's delete effects, which
    /// additionally unlocks the cached-effect fast path. See `IPruningStrategy`.
    bool m_requires_delete_effects;
    double m_atom_first_ratio;
    PruningStrategy m_pruning_strategy;

    std::vector<formalism::GroundAction> m_filtered_actions;
    std::vector<iw::AtomIndexList> m_action_add_atoms;
    iw::AtomIndexList m_single_action_add_atoms;
    iw::AtomIndexList m_single_action_del_atoms;
    /// Only used on the empty-delete path, so it is allocated once and never written.
    iw::AtomIndexList m_no_del_atoms;
    std::vector<uint8_t> m_selected_action_mask;
    std::vector<Index> m_remaining_atoms;
    std::vector<uint8_t> m_atom_in_remaining;
    absl::flat_hash_map<Index, std::vector<size_t>> m_atom_to_action_indices;

    void refresh_remaining_atoms();

    /// @brief Run the precheck for one action, deriving the transition delta the cheapest way the
    /// action allows. Never builds a successor state.
    bool test_action_with_delete_effects(const State& state, formalism::GroundAction action, StateRepositoryImpl& state_repository);

public:
    IW1ActionPrecheckController(const Options& options, const PruningStrategy& pruning_strategy, const formalism::Problem& problem, const State& start_state);

    [[nodiscard]] bool is_enabled() const { return m_enabled; }
    [[nodiscard]] bool supports_online_filtering() const { return m_enabled && !m_atom_first_mode; }

    bool test_action(const State& state, formalism::GroundAction action, StateRepositoryImpl& state_repository);

    std::span<const formalism::GroundAction>
    filter_actions(const State& state, const std::span<const formalism::GroundAction>& actions, StateRepositoryImpl& state_repository);
};

class IW1IncrementalActionDiscoveryController;
struct IW1IncrementalStatisticsReporter
{
    EventHandler event_handler;
    const IW1IncrementalActionDiscoveryController* controller;

    ~IW1IncrementalStatisticsReporter();
};

void run_incremental_precheck_filtered_crosscheck(const SearchContext& context,
                                                  const Options& options,
                                                  const State& start_state,
                                                  const State& state,
                                                  const PruningStrategy& pruning_strategy,
                                                  StateRepositoryImpl& state_repository,
                                                  const IW1IncrementalActionDiscoveryController& iw1_incremental_action_discovery,
                                                  const std::span<const formalism::GroundAction>& filtered_incremental_actions);

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
                                     StopWatch& stopwatch,
                                     SearchEndGuard& end_guard);

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
                                              StopWatch& stopwatch,
                                              SearchEndGuard& end_guard);
}

#endif
