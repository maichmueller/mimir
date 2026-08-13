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

#include "mimir/search/algorithms/brfs/event_handlers/search_tree.hpp"

#include "mimir/formalism/ground_action.hpp"
#include "mimir/search/state.hpp"

#include <algorithm>
#include <stdexcept>

using namespace mimir::formalism;

namespace mimir::search::brfs
{
void SearchTree::reset(Index root_state_index)
{
    m_nodes.clear();
    m_node_by_state.clear();

    m_nodes.push_back(SearchTreeNode { root_state_index, std::nullopt, kNoIncomingAction, 0 });
    m_node_by_state.emplace(root_state_index, size_t(0));
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

std::vector<Index> SearchTree::extract_action_path(size_t node_index) const
{
    if (node_index >= m_nodes.size())
    {
        throw std::out_of_range("brfs::SearchTree::extract_action_path: node index out of range.");
    }

    auto actions = std::vector<Index> {};
    for (auto current = m_nodes[node_index]; current.parent_node.has_value(); current = m_nodes[*current.parent_node])
    {
        actions.push_back(current.incoming_action);
    }
    std::reverse(actions.begin(), actions.end());
    return actions;
}

std::vector<Index> SearchTree::extract_state_path(size_t node_index) const
{
    if (node_index >= m_nodes.size())
    {
        throw std::out_of_range("brfs::SearchTree::extract_state_path: node index out of range.");
    }

    auto states = std::vector<Index> { m_nodes[node_index].state };
    for (auto current = m_nodes[node_index]; current.parent_node.has_value(); current = m_nodes[*current.parent_node])
    {
        states.push_back(m_nodes[*current.parent_node].state);
    }
    std::reverse(states.begin(), states.end());
    return states;
}

SearchTreeEventHandlerImpl::SearchTreeEventHandlerImpl(Problem problem) : EventHandlerBase<SearchTreeEventHandlerImpl>(std::move(problem), true), m_search_tree()
{
}

SearchTreeEventHandler SearchTreeEventHandlerImpl::create(Problem problem) { return std::make_shared<SearchTreeEventHandlerImpl>(std::move(problem)); }

void SearchTreeEventHandlerImpl::on_start_search(const State& start_state)
{
    EventHandlerBase<SearchTreeEventHandlerImpl>::on_start_search(start_state);

    m_search_tree.reset(start_state.get_index());
}

void SearchTreeEventHandlerImpl::on_generate_state_in_search_tree(const State& state,
                                                                  GroundAction action,
                                                                  ContinuousCost action_cost,
                                                                  const State& successor_state)
{
    EventHandlerBase<SearchTreeEventHandlerImpl>::on_generate_state_in_search_tree(state, action, action_cost, successor_state);

    m_search_tree.add_admitted_transition(state.get_index(), action->get_index(), successor_state.get_index());
}

void SearchTreeEventHandlerImpl::on_generate_state_in_search_tree_without_payload()
{
    throw std::logic_error("brfs::SearchTreeEventHandlerImpl: search-tree capture needs the parent and action of every admitted state, but the search "
                           "reported an admission without payload.");
}
}
