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

#include "mimir/search/algorithms/brfs/event_handlers/observation.hpp"

#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/ground_effects.hpp"
#include "mimir/search/state.hpp"

#include <algorithm>
#include <ranges>
#include <stdexcept>

using namespace mimir::formalism;

namespace mimir::search::brfs
{
namespace
{
/// @brief The fluent atoms of `successor` that `parent` does not have, and vice versa.
///
/// This is what the transition actually changed, which is not the same as what the action could
/// change: a conditional effect whose condition did not hold contributes nothing here, and an add
/// effect for an atom that already held is not a change.
void compute_realized_fluent_delta(const State& parent, const State& successor, iw::AtomIndexList& out_added, iw::AtomIndexList& out_deleted)
{
    const auto& parent_atoms = parent.get_atoms<FluentTag>();
    const auto& successor_atoms = successor.get_atoms<FluentTag>();

    out_added.clear();
    out_deleted.clear();

    for (const auto atom_index : successor_atoms)
    {
        if (!parent_atoms.get(atom_index))
        {
            out_added.push_back(atom_index);
        }
    }
    for (const auto atom_index : parent_atoms)
    {
        if (!successor_atoms.get(atom_index))
        {
            out_deleted.push_back(atom_index);
        }
    }
}

GroundActionEffectSummary compute_effect_summary(GroundAction action)
{
    auto summary = GroundActionEffectSummary {};

    /* Every conditional effect counts, whether or not its condition can ever hold: this describes the
       action's syntax, not one application of it. Atoms repeated across conditional effects count
       once each, for the same reason. */
    for (const auto& conditional_effect : action->get_conditional_effects())
    {
        const auto conjunctive_effect = conditional_effect->get_conjunctive_effect();

        summary.num_add_effects +=
            static_cast<uint32_t>(std::ranges::distance(conjunctive_effect->get_propositional_effects<PositiveTag>()));
        summary.num_delete_effects +=
            static_cast<uint32_t>(std::ranges::distance(conjunctive_effect->get_propositional_effects<NegativeTag>()));
        summary.num_fluent_numeric_effects += static_cast<uint32_t>(conjunctive_effect->get_fluent_numeric_effects().size());
        summary.has_auxiliary_numeric_effect =
            summary.has_auxiliary_numeric_effect || conjunctive_effect->get_auxiliary_numeric_effect().has_value();
    }

    return summary;
}
}

/**
 * SearchTree
 */

void SearchTree::reset(Index root_state_index)
{
    clear();

    m_nodes.push_back(SearchTreeNode { root_state_index, std::nullopt, kNoIncomingAction, 0 });
    m_node_by_state.emplace(root_state_index, size_t(0));
}

void SearchTree::clear()
{
    m_nodes.clear();
    m_node_by_state.clear();
}

void SearchTree::add_admitted_transition(Index parent_state_index, Index action_index, Index successor_state_index)
{
    const auto parent_it = m_node_by_state.find(parent_state_index);
    if (parent_it == m_node_by_state.end())
    {
        /* The search admitted a successor of a state that was never itself admitted, which would
           mean this handler missed the parent's own admission. A tree with a detached branch reads
           like a tree, so say so instead. */
        throw std::logic_error("brfs::SearchTree::add_admitted_transition: the parent state was never admitted into the search tree.");
    }
    const auto parent_node_index = parent_it->second;
    const auto depth = m_nodes[parent_node_index].depth + 1;

    const auto [it, inserted] = m_node_by_state.emplace(successor_state_index, m_nodes.size());
    if (!inserted)
    {
        // Re-admission: keep the node's place in admission order but adopt the newer parent edge,
        // which is what the search's own predecessor relation does.
        auto& node = m_nodes[it->second];
        node.parent_node = parent_node_index;
        node.incoming_action = action_index;
        node.depth = depth;
        return;
    }

    m_nodes.push_back(SearchTreeNode { successor_state_index, parent_node_index, action_index, depth });
}

std::optional<size_t> SearchTree::find_node_by_state(Index state_index) const
{
    const auto it = m_node_by_state.find(state_index);
    return (it == m_node_by_state.end()) ? std::nullopt : std::optional<size_t>(it->second);
}

std::optional<uint32_t> SearchTree::find_depth_by_state(Index state_index) const
{
    const auto it = m_node_by_state.find(state_index);
    return (it == m_node_by_state.end()) ? std::nullopt : std::optional<uint32_t>(m_nodes[it->second].depth);
}

void SearchTree::visit_to_root(size_t node_index, const std::function<void(const SearchTreeNode&)>& visitor) const
{
    if (node_index >= m_nodes.size())
    {
        throw std::out_of_range("brfs::SearchTree: node index out of range.");
    }

    /* A well-formed tree reaches the root in fewer than `size()` steps, because every step moves to a
       strictly earlier node. Bounding the walk turns a corrupted parent link into an exception
       instead of a hang. */
    auto remaining_steps = m_nodes.size();
    auto current_index = node_index;
    while (true)
    {
        const auto& node = m_nodes[current_index];
        visitor(node);

        if (!node.parent_node.has_value())
        {
            return;
        }

        if (remaining_steps-- == 0)
        {
            throw std::logic_error("brfs::SearchTree: parent links do not reach the root; the tree is cyclic or corrupted.");
        }

        current_index = *node.parent_node;
        if (current_index >= m_nodes.size())
        {
            throw std::logic_error("brfs::SearchTree: parent index out of range; the tree is corrupted.");
        }
    }
}

std::vector<Index> SearchTree::get_action_indices(size_t node_index) const
{
    auto actions = std::vector<Index> {};
    // The root is visited too but contributes nothing: it was not reached by an action.
    visit_to_root(node_index,
                  [&actions](const SearchTreeNode& node)
                  {
                      if (node.parent_node.has_value())
                      {
                          actions.push_back(node.incoming_action);
                      }
                  });
    std::reverse(actions.begin(), actions.end());
    return actions;
}

std::vector<Index> SearchTree::get_state_indices(size_t node_index) const
{
    auto states = std::vector<Index> {};
    visit_to_root(node_index, [&states](const SearchTreeNode& node) { states.push_back(node.state); });
    std::reverse(states.begin(), states.end());
    return states;
}

/**
 * Observation
 */

void Observation::clear()
{
    m_search_tree.clear();
    m_transitions.clear();
    m_action_effect_summaries.clear();
}

const GroundActionEffectSummary& Observation::intern_action_effect_summary(GroundAction action)
{
    const auto action_index = action->get_index();
    const auto it = m_action_effect_summaries.find(action_index);
    if (it != m_action_effect_summaries.end())
    {
        return it->second;
    }

    return m_action_effect_summaries.emplace(action_index, compute_effect_summary(action)).first->second;
}

const GroundActionEffectSummary* Observation::find_action_effect_summary(Index action_index) const
{
    const auto it = m_action_effect_summaries.find(action_index);
    return (it == m_action_effect_summaries.end()) ? nullptr : &it->second;
}

/**
 * Aggregates
 */

TransitionAggregates compute_transition_aggregates(const TransitionObservationList& transitions)
{
    auto aggregates = TransitionAggregates {};
    aggregates.num_transitions = transitions.size();

    const auto grow_to = [](std::vector<uint64_t>& counters, size_t depth)
    {
        if (counters.size() <= depth)
        {
            counters.resize(depth + 1, 0);
        }
    };

    for (const auto& transition : transitions)
    {
        const auto depth = static_cast<size_t>(transition.successor_depth);

        if (transition.disposition == TransitionDisposition::ADMITTED)
        {
            ++aggregates.num_admitted;
            grow_to(aggregates.num_admitted_by_depth, depth);
            ++aggregates.num_admitted_by_depth[depth];

            if (transition.action_effect_summary)
            {
                ++aggregates.num_admitted_with_effect_summary;
                aggregates.total_add_effects += transition.action_effect_summary->num_add_effects;
                aggregates.max_add_effects = std::max(aggregates.max_add_effects, transition.action_effect_summary->num_add_effects);
                aggregates.total_delete_effects += transition.action_effect_summary->num_delete_effects;
                aggregates.max_delete_effects = std::max(aggregates.max_delete_effects, transition.action_effect_summary->num_delete_effects);
            }
        }
        else
        {
            ++aggregates.num_rejected;
            grow_to(aggregates.num_rejected_by_depth, depth);
            ++aggregates.num_rejected_by_depth[depth];
        }

        if (transition.novel_fluent_atom_indices.has_value())
        {
            const auto witness_size = static_cast<uint32_t>(transition.novel_fluent_atom_indices->size());
            ++aggregates.num_transitions_with_witness;
            aggregates.total_witness_size += witness_size;
            aggregates.max_witness_size = std::max(aggregates.max_witness_size, witness_size);

            grow_to(aggregates.total_witness_size_by_depth, depth);
            grow_to(aggregates.num_transitions_with_witness_by_depth, depth);
            aggregates.total_witness_size_by_depth[depth] += witness_size;
            ++aggregates.num_transitions_with_witness_by_depth[depth];
        }

        if (transition.realized_added_fluent_atom_indices.has_value())
        {
            aggregates.total_realized_added += transition.realized_added_fluent_atom_indices->size();
        }
        if (transition.realized_deleted_fluent_atom_indices.has_value())
        {
            aggregates.total_realized_deleted += transition.realized_deleted_fluent_atom_indices->size();
        }
    }

    return aggregates;
}

/**
 * ObservationEventHandlerImpl
 */

ObservationEventHandlerImpl::ObservationEventHandlerImpl(Problem problem, ObservationOptions options) :
    EventHandlerBase<ObservationEventHandlerImpl>(std::move(problem), true),
    m_options(options),
    m_observation(),
    m_pending_witnesses(),
    m_realized_added_scratch(),
    m_realized_deleted_scratch()
{
}

ObservationEventHandler ObservationEventHandlerImpl::create(Problem problem, ObservationOptions options)
{
    return std::make_shared<ObservationEventHandlerImpl>(std::move(problem), options);
}

void ObservationEventHandlerImpl::on_start_search(const State& start_state)
{
    EventHandlerBase<ObservationEventHandlerImpl>::on_start_search(start_state);

    m_observation.clear();
    m_pending_witnesses.clear();

    /* The tree is rooted even when only transitions were asked for: transition depths are read off
       the tree, and a transition out of an unknown parent has no depth to report. */
    m_observation.get_mutable_search_tree().reset(start_state.get_index());
}

void ObservationEventHandlerImpl::on_generate_state_with_novel_witness(const State& state,
                                                                       GroundAction action,
                                                                       ContinuousCost action_cost,
                                                                       const State& successor_state,
                                                                       const iw::AtomIndexList& novel_fluent_atom_indices)
{
    [[maybe_unused]] const auto ignored_action_cost = action_cost;

    if (!m_options.capture_novel_witnesses)
    {
        return;
    }

    /* Keyed by the transition rather than kept in a single slot: the beam generates a whole layer
       before classifying any of it, so the witness of one transition and the classification of
       another interleave freely. */
    m_pending_witnesses[TransitionKey { state.get_index(), action->get_index(), successor_state.get_index() }] = novel_fluent_atom_indices;
}

void ObservationEventHandlerImpl::record_transition(const State& state,
                                                    GroundAction action,
                                                    ContinuousCost action_cost,
                                                    const State& successor_state,
                                                    TransitionDisposition disposition)
{
    const auto& tree = m_observation.get_search_tree();
    const auto parent_depth = tree.find_depth_by_state(state.get_index()).value_or(0);

    auto transition = TransitionObservation {};
    transition.parent_state = state.get_index();
    transition.action = action->get_index();
    transition.successor_state = successor_state.get_index();
    transition.parent_depth = parent_depth;
    transition.successor_depth = parent_depth + 1;
    transition.action_cost = action_cost;
    transition.disposition = disposition;

    if (m_options.capture_novel_witnesses)
    {
        const auto key = TransitionKey { state.get_index(), action->get_index(), successor_state.get_index() };
        const auto it = m_pending_witnesses.find(key);
        if (it != m_pending_witnesses.end())
        {
            transition.novel_fluent_atom_indices = std::move(it->second);
            m_pending_witnesses.erase(it);
        }
        /* Otherwise the witness stays empty: the pruning strategy could not answer witness queries,
           which is a different statement from "the query ran and found nothing". */
    }

    if (m_options.capture_action_effect_summaries)
    {
        transition.action_effect_summary = &m_observation.intern_action_effect_summary(action);
    }

    if (m_options.capture_realized_effects)
    {
        compute_realized_fluent_delta(state, successor_state, m_realized_added_scratch, m_realized_deleted_scratch);
        transition.realized_added_fluent_atom_indices = m_realized_added_scratch;
        transition.realized_deleted_fluent_atom_indices = m_realized_deleted_scratch;
    }

    m_observation.get_mutable_transitions().push_back(std::move(transition));
}

void ObservationEventHandlerImpl::on_generate_state_in_search_tree(const State& state,
                                                                    GroundAction action,
                                                                    ContinuousCost action_cost,
                                                                    const State& successor_state)
{
    EventHandlerBase<ObservationEventHandlerImpl>::on_generate_state_in_search_tree(state, action, action_cost, successor_state);

    /* Order matters: the transition's depths come from the tree, and the record is written in
       classification order, so the successor has to be in the tree before the record is made. */
    if (m_options.capture_admitted_transitions || m_options.capture_rejected_transitions)
    {
        record_transition(state, action, action_cost, successor_state, TransitionDisposition::ADMITTED);
    }

    /* The tree is maintained unconditionally: it is where a transition's depths come from, and one
       node per admitted state is negligible next to the search that produced it. */
    m_observation.get_mutable_search_tree().add_admitted_transition(state.get_index(), action->get_index(), successor_state.get_index());
}

void ObservationEventHandlerImpl::on_generate_state_not_in_search_tree(const State& state,
                                                                        GroundAction action,
                                                                        ContinuousCost action_cost,
                                                                        const State& successor_state)
{
    EventHandlerBase<ObservationEventHandlerImpl>::on_generate_state_not_in_search_tree(state, action, action_cost, successor_state);

    if (m_options.capture_rejected_transitions)
    {
        record_transition(state, action, action_cost, successor_state, TransitionDisposition::REJECTED);
    }
    else if (m_options.capture_novel_witnesses)
    {
        // Nobody will claim this witness, so do not let the pending map grow for the whole search.
        m_pending_witnesses.erase(TransitionKey { state.get_index(), action->get_index(), successor_state.get_index() });
    }
}

void ObservationEventHandlerImpl::on_generate_state_in_search_tree_without_payload()
{
    throw std::logic_error("brfs::ObservationEventHandlerImpl: observation needs the parent and action of every admitted state, but the search reported "
                           "an admission without payload.");
}

Observation ObservationEventHandlerImpl::take_observation()
{
    auto observation = std::move(m_observation);
    m_observation = Observation();
    m_pending_witnesses.clear();
    return observation;
}
}
