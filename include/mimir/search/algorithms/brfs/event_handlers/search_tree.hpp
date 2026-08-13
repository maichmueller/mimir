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

#ifndef MIMIR_SEARCH_ALGORITHMS_BRFS_EVENT_HANDLERS_SEARCH_TREE_HPP_
#define MIMIR_SEARCH_ALGORITHMS_BRFS_EVENT_HANDLERS_SEARCH_TREE_HPP_

#include "mimir/search/algorithms/brfs/event_handlers/interface.hpp"

#include <absl/container/flat_hash_map.h>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace mimir::search::brfs
{

/// @brief One admitted state, described by native indices rather than by `State`/`GroundAction`
/// handles.
///
/// A `State` owns a pooled unpacked-state slot, so a tree that stored states would hold one slot per
/// admitted node and drain the pool on any search worth capturing. Indices are stable for the
/// lifetime of the search context, so a caller can turn them back into states and actions after the
/// search instead of during its events.
struct SearchTreeNode
{
    Index state;                        ///< Index of the admitted state.
    std::optional<size_t> parent_node;  ///< Node index of the parent; empty exactly for the root.
    Index incoming_action;              ///< Index of the action that produced this state; meaningless for the root.
    uint32_t depth;                     ///< Number of actions from the root -- not accumulated action cost.
};

/// @brief The states a BrFS run admitted into its search tree, in admission order, with node `0` the
/// root.
class SearchTree
{
public:
    /// @brief Sentinel stored in `SearchTreeNode::incoming_action` for the root, which has none.
    static constexpr auto kNoIncomingAction = std::numeric_limits<Index>::max();

    /// @brief Discard everything and start a fresh tree rooted at `root_state_index`.
    void reset(Index root_state_index);

    /// @brief Record that `action_index` led from `parent_state_index` to `successor_state_index`.
    ///
    /// Re-admitting a state updates its existing node in place rather than appending a second one,
    /// mirroring the search node whose parent the search itself overwrites. Every pruning strategy
    /// currently in use rejects an already-admitted successor, so this is a defensive path.
    void add_admitted_transition(Index parent_state_index, Index action_index, Index successor_state_index);

    /// @brief The node index at which `state_index` was admitted, or empty if it never was.
    std::optional<size_t> find_node_by_state(Index state_index) const;

    /// @brief The action indices from the root down to `node_index`, in application order. Empty for
    /// the root.
    /// @throws std::out_of_range if `node_index` is not a node of this tree.
    std::vector<Index> extract_action_path(size_t node_index) const;

    /// @brief The state indices from the root down to `node_index`, starting at the root.
    /// @throws std::out_of_range if `node_index` is not a node of this tree.
    std::vector<Index> extract_state_path(size_t node_index) const;

    const std::vector<SearchTreeNode>& get_nodes() const { return m_nodes; }
    size_t get_num_nodes() const { return m_nodes.size(); }

private:
    std::vector<SearchTreeNode> m_nodes;
    absl::flat_hash_map<Index, size_t> m_node_by_state;
};

/**
 * Implementation class
 */

/// @brief A quiet event handler that additionally records the admitted search tree.
///
/// It derives from `EventHandlerBase`, so a caller that captures the tree also gets the full
/// statistics of the run for free. It stays quiet -- it prints nothing and calls no `_impl` hook --
/// and only intercepts the two events the tree is built from.
///
/// One handler captures one search. `on_start_search` clears the tree, so reusing an instance across
/// the several BrFS passes of a multi-arity IW leaves only the last pass's tree: give each pass its
/// own handler when every arity's tree is wanted.
class SearchTreeEventHandlerImpl : public EventHandlerBase<SearchTreeEventHandlerImpl>
{
private:
    friend class EventHandlerBase<SearchTreeEventHandlerImpl>;

    /* This handler is always quiet, so the base never reaches these -- but the base is a template and
       instantiates the calls regardless, so they have to exist. */
    void on_expand_state_impl(const State&) const {}
    void on_expand_goal_state_impl(const State&) const {}
    void on_generate_state_impl(const State&, formalism::GroundAction, ContinuousCost, const State&) const {}
    void on_generate_state_in_search_tree_impl(const State&, formalism::GroundAction, ContinuousCost, const State&) const {}
    void on_generate_state_not_in_search_tree_impl(const State&, formalism::GroundAction, ContinuousCost, const State&) const {}
    void on_finish_g_layer_impl(uint32_t, uint64_t, uint64_t) const {}
    void on_start_search_impl(const State&) const {}
    void on_end_search_impl(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) const {}
    void on_solved_impl(const Plan&) const {}
    void on_unsolvable_impl() const {}
    void on_exhausted_impl() const {}

    SearchTree m_search_tree;

public:
    explicit SearchTreeEventHandlerImpl(formalism::Problem problem);

    static SearchTreeEventHandler create(formalism::Problem problem);

    void on_start_search(const State& start_state) override;

    void on_generate_state_in_search_tree(const State& state, formalism::GroundAction action, ContinuousCost action_cost, const State& successor_state) override;

    /* Capturing the tree says nothing about wanting novelty witnesses, and computing a witness costs
       a scan per transition. This handler never reads one, so it never asks for one. */
    bool supports_novel_witness_events() const override { return false; }

    /// @brief The staged parallel-beam fast path reports *rejections* without payload but still
    /// admits through the payloadful `on_generate_state_in_search_tree`, which is the only event this
    /// handler builds from -- so tree capture keeps that fast path. A future path that admitted
    /// without payload would silently drop edges, so fail loudly rather than return a tree with
    /// holes in it.
    void on_generate_state_in_search_tree_without_payload() override;

    const SearchTree& get_search_tree() const { return m_search_tree; }
};

}

#endif
