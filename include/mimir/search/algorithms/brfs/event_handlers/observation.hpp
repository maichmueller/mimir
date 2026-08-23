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

#ifndef MIMIR_SEARCH_ALGORITHMS_BRFS_EVENT_HANDLERS_OBSERVATION_HPP_
#define MIMIR_SEARCH_ALGORITHMS_BRFS_EVENT_HANDLERS_OBSERVATION_HPP_

#include "mimir/search/algorithms/brfs/event_handlers/interface.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/node_hash_map.h>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

namespace mimir::search::brfs
{

/**
 * Search tree
 */

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
///
/// Paths are reconstructed on demand by walking parent indices, not stored per node: a stored path
/// per node costs O(nodes * depth) for information the parent links already carry.
class SearchTree
{
public:
    /// @brief Sentinel stored in `SearchTreeNode::incoming_action` for the root, which has none.
    static constexpr auto kNoIncomingAction = std::numeric_limits<Index>::max();

    /// @brief Discard everything and start a fresh tree rooted at `root_state_index`.
    void reset(Index root_state_index);

    /// @brief Forget everything, including the root. A tree that never saw a search is empty rather
    /// than rooted at an arbitrary state.
    void clear();

    /// @brief Record that `action_index` led from `parent_state_index` to `successor_state_index`.
    ///
    /// Re-admitting a state updates its existing node in place rather than appending a second one,
    /// mirroring the search node whose parent the search itself overwrites. Every pruning strategy
    /// currently in use rejects an already-admitted successor, so this is a defensive path.
    void add_admitted_transition(Index parent_state_index, Index action_index, Index successor_state_index);

    /// @brief The node index at which `state_index` was admitted, or empty if it never was.
    std::optional<size_t> find_node_by_state(Index state_index) const;

    /// @brief The depth of `node_index`, or empty if there is no such node.
    std::optional<uint32_t> find_depth_by_state(Index state_index) const;

    /// @brief The action indices from the root down to `node_index`, in application order. Empty for
    /// the root.
    /// @throws std::out_of_range if `node_index` is not a node of this tree.
    /// @throws std::logic_error if the parent links do not reach the root within `size()` steps.
    std::vector<Index> get_action_indices(size_t node_index) const;

    /// @brief The state indices from the root down to `node_index`, starting at the root. Always one
    /// longer than the action path.
    /// @throws std::out_of_range if `node_index` is not a node of this tree.
    /// @throws std::logic_error if the parent links do not reach the root within `size()` steps.
    std::vector<Index> get_state_indices(size_t node_index) const;

    const std::vector<SearchTreeNode>& get_nodes() const { return m_nodes; }
    size_t get_num_nodes() const { return m_nodes.size(); }

private:
    /// @brief Walk parent links from `node_index` to the root, refusing to loop forever on a tree
    /// whose links were corrupted.
    void visit_to_root(size_t node_index, const std::function<void(const SearchTreeNode&)>& visitor) const;

    std::vector<SearchTreeNode> m_nodes;
    absl::flat_hash_map<Index, size_t> m_node_by_state;
};

/**
 * Transitions
 */

/// @brief Whether a generated transition entered the search tree or was rejected.
enum class TransitionDisposition
{
    ADMITTED,
    REJECTED
};

/// @brief How many effects the *grounded action* syntactically has, independent of any state.
///
/// Counted over every grounded conditional effect without evaluating its condition, and without
/// deduplicating atoms that appear in more than one of them. This describes the action, so it is
/// cached once per action index; what a particular transition actually changed is the realized
/// delta on `TransitionObservation` instead.
struct GroundActionEffectSummary
{
    uint32_t num_add_effects = 0;
    uint32_t num_delete_effects = 0;
    uint32_t num_fluent_numeric_effects = 0;
    bool has_auxiliary_numeric_effect = false;
};

/// @brief One generated transition, with the classification it eventually received.
struct TransitionObservation
{
    Index parent_state;
    Index action;
    Index successor_state;

    uint32_t parent_depth;
    /// The successor's depth. For an admitted transition this is `parent_depth + 1`; a rejected
    /// successor is not in the tree, so this is the depth it *would* have had.
    uint32_t successor_depth;
    ContinuousCost action_cost;

    TransitionDisposition disposition;

    /// The fluent atoms this transition made novel. Empty optional means the witness was not
    /// requested or the pruning strategy cannot answer witness queries; an empty *list* means the
    /// query ran and found no novel atom.
    std::optional<iw::AtomIndexList> novel_fluent_atom_indices;

    /// Fluent atoms that hold in the successor but not in the parent, and vice versa. Empty optional
    /// unless realized-effect capture was requested.
    std::optional<iw::AtomIndexList> realized_added_fluent_atom_indices;
    std::optional<iw::AtomIndexList> realized_deleted_fluent_atom_indices;

    /// Borrowed from the owning `Observation`'s cache, or null when effect summaries were not
    /// requested. Never owned here: one action is taken by many transitions.
    const GroundActionEffectSummary* action_effect_summary = nullptr;
};

using TransitionObservationList = std::vector<TransitionObservation>;

/**
 * Options
 */

/// @brief What an `ObservationEventHandlerImpl` should collect.
///
/// Everything is off by default and each flag turns on only its own work: tree capture records
/// parent/action/state per admitted state, transition capture keeps a record per classified
/// transition, witness capture makes the search compute novelty witnesses at all, effect summaries
/// inspect each observed action once, and realized effects difference every parent/successor pair.
struct ObservationOptions
{
    /// Record the admitted search tree.
    bool capture_search_tree = false;
    /// Keep a `TransitionObservation` per admitted transition.
    bool capture_admitted_transitions = false;
    /// Also keep one per rejected transition. Implies `capture_admitted_transitions` in the sense
    /// that both dispositions then land in the same log.
    bool capture_rejected_transitions = false;
    /// Ask the search for novelty witnesses and attach them to their transition.
    bool capture_novel_witnesses = false;
    /// Cache a syntactic effect summary per observed action index.
    bool capture_action_effect_summaries = false;
    /// Difference each transition's parent and successor fluent atoms.
    bool capture_realized_effects = false;

    /// @brief Whether any transition record is kept at all.
    bool captures_transitions() const { return capture_admitted_transitions || capture_rejected_transitions; }
};

/**
 * Observation
 */

/// @brief Everything one BrFS run was asked to record.
///
/// Transitions are appended in *classification* order -- the order in which the search decided each
/// transition's fate -- not in generation order. On the queued path the two coincide, since a
/// transition is classified the moment it is generated. A beam generates a whole layer before
/// classifying any of it, and there classification order is admission order, which is the order that
/// says what the beam kept. Tree nodes are in admission order for the same reason, so a node's parent
/// always appears before it.
///
/// Non-copyable on purpose: `TransitionObservation::action_effect_summary` points into
/// `m_action_effect_summaries`, and a copy would leave every one of those pointers aimed at the
/// original. Hand it out by reference (keeping its owner alive) or move it.
class Observation
{
public:
    Observation() = default;
    Observation(const Observation&) = delete;
    Observation& operator=(const Observation&) = delete;
    Observation(Observation&&) = default;
    Observation& operator=(Observation&&) = default;

    void clear();

    /// @brief The cached summary for `action_index`, computing it on first sight.
    const GroundActionEffectSummary& intern_action_effect_summary(formalism::GroundAction action);

    /// @brief The cached summary for `action_index`, or null if that action was never summarized.
    const GroundActionEffectSummary* find_action_effect_summary(Index action_index) const;

    SearchTree& get_mutable_search_tree() { return m_search_tree; }
    TransitionObservationList& get_mutable_transitions() { return m_transitions; }

    const SearchTree& get_search_tree() const { return m_search_tree; }
    const TransitionObservationList& get_transitions() const { return m_transitions; }
    size_t get_num_action_effect_summaries() const { return m_action_effect_summaries.size(); }

private:
    SearchTree m_search_tree;
    TransitionObservationList m_transitions;
    /// Node-based so the pointers handed to `TransitionObservation` survive later insertions.
    absl::node_hash_map<Index, GroundActionEffectSummary> m_action_effect_summaries;
};

/**
 * Aggregates
 */

/// @brief Derived totals over a transition log, computed natively so Python never has to walk the
/// records to answer "how big were the witnesses" or "how much was rejected at depth 3".
struct TransitionAggregates
{
    uint64_t num_transitions = 0;
    uint64_t num_admitted = 0;
    uint64_t num_rejected = 0;

    /// Only over transitions that carry a witness list at all.
    uint64_t num_transitions_with_witness = 0;
    uint64_t total_witness_size = 0;
    uint32_t max_witness_size = 0;

    /// Only over admitted transitions that carry an effect summary.
    uint64_t num_admitted_with_effect_summary = 0;
    uint64_t total_add_effects = 0;
    uint32_t max_add_effects = 0;
    uint64_t total_delete_effects = 0;
    uint32_t max_delete_effects = 0;

    uint64_t total_realized_added = 0;
    uint64_t total_realized_deleted = 0;

    /// Indexed by successor depth.
    std::vector<uint64_t> num_admitted_by_depth;
    std::vector<uint64_t> num_rejected_by_depth;
    std::vector<uint64_t> total_witness_size_by_depth;
    std::vector<uint64_t> num_transitions_with_witness_by_depth;

    double get_average_witness_size() const
    {
        return (num_transitions_with_witness == 0) ? 0.0 : static_cast<double>(total_witness_size) / static_cast<double>(num_transitions_with_witness);
    }
    double get_average_add_effects() const
    {
        return (num_admitted_with_effect_summary == 0) ? 0.0 : static_cast<double>(total_add_effects) / static_cast<double>(num_admitted_with_effect_summary);
    }
    double get_average_delete_effects() const
    {
        return (num_admitted_with_effect_summary == 0) ? 0.0 : static_cast<double>(total_delete_effects) / static_cast<double>(num_admitted_with_effect_summary);
    }
};

extern TransitionAggregates compute_transition_aggregates(const TransitionObservationList& transitions);

/**
 * Implementation class
 */

/// @brief A quiet event handler that records what `ObservationOptions` asks for.
///
/// It derives from `EventHandlerBase`, so anything it observes comes with the full statistics of the
/// run. It stays quiet -- it prints nothing and calls no `_impl` hook -- and only intercepts the
/// events its options need.
///
/// One handler observes one search. `on_start_search` clears the observation, so a caller that wants
/// each pass of a multi-arity IW separately has to take the observation between passes; that is what
/// `iw::ObservationEventHandlerImpl` does.
class ObservationEventHandlerImpl : public EventHandlerBase<ObservationEventHandlerImpl>
{
private:
    friend class EventHandlerBase<ObservationEventHandlerImpl>;

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

    /// Identifies a transition independently of when its events arrive.
    struct TransitionKey
    {
        Index parent_state;
        Index action;
        Index successor_state;

        bool operator==(const TransitionKey& other) const = default;

        template<typename H>
        friend H AbslHashValue(H hash, const TransitionKey& key)
        {
            return H::combine(std::move(hash), key.parent_state, key.action, key.successor_state);
        }
    };

    void record_transition(const State& state,
                           formalism::GroundAction action,
                           ContinuousCost action_cost,
                           const State& successor_state,
                           TransitionDisposition disposition);

    ObservationOptions m_options;
    Observation m_observation;
    /// Witnesses reported for transitions that have not been classified yet. The beam classifies a
    /// whole layer at once, long after generating it, so a single "last witness" slot would attach
    /// the wrong atoms to the wrong transition.
    absl::flat_hash_map<TransitionKey, iw::AtomIndexList> m_pending_witnesses;
    /// Reused scratch for realized-effect differencing.
    iw::AtomIndexList m_realized_added_scratch;
    iw::AtomIndexList m_realized_deleted_scratch;

public:
    ObservationEventHandlerImpl(formalism::Problem problem, ObservationOptions options);

    static ObservationEventHandler create(formalism::Problem problem, ObservationOptions options);

    void on_start_search(const State& start_state) override;

    void on_generate_state_in_search_tree(const State& state, formalism::GroundAction action, ContinuousCost action_cost, const State& successor_state) override;

    void
    on_generate_state_not_in_search_tree(const State& state, formalism::GroundAction action, ContinuousCost action_cost, const State& successor_state) override;

    /// Only when witnesses were explicitly asked for. Capturing a tree or transitions says nothing
    /// about wanting witnesses, and a witness costs a scan of the transition per generated state.
    bool supports_novel_witness_events() const override { return m_options.capture_novel_witnesses; }

    void on_generate_state_with_novel_witness(const State& state,
                                              formalism::GroundAction action,
                                              ContinuousCost action_cost,
                                              const State& successor_state,
                                              const iw::AtomIndexList& novel_fluent_atom_indices) override;

    /// @brief The staged parallel-beam fast path reports *rejections* without payload while still
    /// admitting through the payloadful `on_generate_state_in_search_tree`. That is enough for the
    /// tree and for admitted-only transition capture, so those keep the fast path; anything that
    /// needs the rejected side or a witness gives it up rather than returning a log with holes.
    bool supports_payloadless_generated_state_events() const override
    {
        return !m_options.capture_rejected_transitions && !m_options.capture_novel_witnesses;
    }

    /// @brief No current search path admits without payload. If one ever does, tree and transition
    /// capture would silently drop those edges, so fail loudly instead.
    void on_generate_state_in_search_tree_without_payload() override;

    const ObservationOptions& get_options() const { return m_options; }
    const Observation& get_observation() const { return m_observation; }

    /// @brief Move the observation out, leaving this handler ready for the next search. Used to keep
    /// one arity pass's observation before the next pass overwrites it.
    Observation take_observation();
};

}

#endif
