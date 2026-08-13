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

#include "mimir/search/algorithms/brfs/event_handlers/composite.hpp"

#include <algorithm>
#include <stdexcept>

using namespace mimir::formalism;

namespace mimir::search::brfs
{
CompositeEventHandlerImpl::CompositeEventHandlerImpl(std::vector<EventHandler> handlers, size_t statistics_source) :
    m_handlers(std::move(handlers)),
    m_statistics_source(statistics_source)
{
    if (m_handlers.empty())
    {
        throw std::invalid_argument("brfs::CompositeEventHandlerImpl: at least one handler is required.");
    }
    if (std::any_of(m_handlers.begin(), m_handlers.end(), [](const auto& handler) { return handler == nullptr; }))
    {
        throw std::invalid_argument("brfs::CompositeEventHandlerImpl: handlers must not be null.");
    }
    if (m_statistics_source >= m_handlers.size())
    {
        throw std::invalid_argument("brfs::CompositeEventHandlerImpl: statistics_source is not one of the handlers.");
    }
}

EventHandler CompositeEventHandlerImpl::create(std::vector<EventHandler> handlers, size_t statistics_source)
{
    return std::make_shared<CompositeEventHandlerImpl>(std::move(handlers), statistics_source);
}

void CompositeEventHandlerImpl::on_expand_state(const State& state)
{
    for (const auto& handler : m_handlers)
    {
        handler->on_expand_state(state);
    }
}

void CompositeEventHandlerImpl::on_expand_goal_state(const State& state)
{
    for (const auto& handler : m_handlers)
    {
        handler->on_expand_goal_state(state);
    }
}

void CompositeEventHandlerImpl::on_generate_state(const State& state, GroundAction action, ContinuousCost action_cost, const State& successor_state)
{
    for (const auto& handler : m_handlers)
    {
        handler->on_generate_state(state, action, action_cost, successor_state);
    }
}

bool CompositeEventHandlerImpl::supports_novel_witness_events() const
{
    /* Any child that wants witnesses gets them. The others are handed an event they ignore, which is
       cheaper than denying the one child that asked. */
    return std::any_of(m_handlers.begin(), m_handlers.end(), [](const auto& handler) { return handler->supports_novel_witness_events(); });
}

void CompositeEventHandlerImpl::on_generate_state_with_novel_witness(const State& state,
                                                                      GroundAction action,
                                                                      ContinuousCost action_cost,
                                                                      const State& successor_state,
                                                                      const iw::AtomIndexList& novel_fluent_atom_indices)
{
    for (const auto& handler : m_handlers)
    {
        handler->on_generate_state_with_novel_witness(state, action, action_cost, successor_state, novel_fluent_atom_indices);
    }
}

bool CompositeEventHandlerImpl::supports_payloadless_generated_state_events() const
{
    /* Unanimous, unlike witnesses: a payloadless event carries no parent and no action, so one child
       that needs them would silently lose every transition reported that way. */
    return std::all_of(m_handlers.begin(),
                       m_handlers.end(),
                       [](const auto& handler) { return handler->supports_payloadless_generated_state_events(); });
}

void CompositeEventHandlerImpl::on_generate_state_without_payload()
{
    for (const auto& handler : m_handlers)
    {
        handler->on_generate_state_without_payload();
    }
}

void CompositeEventHandlerImpl::on_generate_state_in_search_tree_without_payload()
{
    for (const auto& handler : m_handlers)
    {
        handler->on_generate_state_in_search_tree_without_payload();
    }
}

void CompositeEventHandlerImpl::on_generate_state_not_in_search_tree_without_payload()
{
    for (const auto& handler : m_handlers)
    {
        handler->on_generate_state_not_in_search_tree_without_payload();
    }
}

void CompositeEventHandlerImpl::on_generate_state_in_search_tree(const State& state,
                                                                  GroundAction action,
                                                                  ContinuousCost action_cost,
                                                                  const State& successor_state)
{
    for (const auto& handler : m_handlers)
    {
        handler->on_generate_state_in_search_tree(state, action, action_cost, successor_state);
    }
}

void CompositeEventHandlerImpl::on_generate_state_not_in_search_tree(const State& state,
                                                                      GroundAction action,
                                                                      ContinuousCost action_cost,
                                                                      const State& successor_state)
{
    for (const auto& handler : m_handlers)
    {
        handler->on_generate_state_not_in_search_tree(state, action, action_cost, successor_state);
    }
}

void CompositeEventHandlerImpl::on_finish_g_layer(DiscreteCost g_value)
{
    for (const auto& handler : m_handlers)
    {
        handler->on_finish_g_layer(g_value);
    }
}

void CompositeEventHandlerImpl::on_start_search(const State& start_state)
{
    for (const auto& handler : m_handlers)
    {
        handler->on_start_search(start_state);
    }
}

void CompositeEventHandlerImpl::on_end_search(uint64_t num_reached_fluent_atoms,
                                                uint64_t num_reached_derived_atoms,
                                                uint64_t num_states,
                                                uint64_t num_nodes,
                                                uint64_t num_actions,
                                                uint64_t num_axioms)
{
    for (const auto& handler : m_handlers)
    {
        handler->on_end_search(num_reached_fluent_atoms, num_reached_derived_atoms, num_states, num_nodes, num_actions, num_axioms);
    }
}

void CompositeEventHandlerImpl::on_finish_iw1_incremental_first_applicability(
    const IW1IncrementalFirstApplicabilityStatistics& iw1_incremental_first_applicability_statistics)
{
    for (const auto& handler : m_handlers)
    {
        handler->on_finish_iw1_incremental_first_applicability(iw1_incremental_first_applicability_statistics);
    }
}

void CompositeEventHandlerImpl::on_finish_parallel_beam_chunk(size_t chunk_size,
                                                                std::chrono::nanoseconds worker_compute_time,
                                                                std::chrono::nanoseconds main_thread_merge_time,
                                                                std::chrono::nanoseconds main_thread_intern_time,
                                                                std::chrono::nanoseconds fluent_slot_time,
                                                                std::chrono::nanoseconds numeric_slot_time,
                                                                std::chrono::nanoseconds derived_slot_time,
                                                                std::chrono::nanoseconds state_lookup_time,
                                                                std::chrono::nanoseconds reached_atom_update_time)
{
    for (const auto& handler : m_handlers)
    {
        handler->on_finish_parallel_beam_chunk(chunk_size,
                                               worker_compute_time,
                                               main_thread_merge_time,
                                               main_thread_intern_time,
                                               fluent_slot_time,
                                               numeric_slot_time,
                                               derived_slot_time,
                                               state_lookup_time,
                                               reached_atom_update_time);
    }
}

void CompositeEventHandlerImpl::on_finish_parallel_beam_pipeline(size_t ready_queue_high_water,
                                                                   size_t in_flight_chunks_high_water,
                                                                   std::chrono::nanoseconds consumer_stall_time,
                                                                   std::chrono::nanoseconds producer_stall_time)
{
    for (const auto& handler : m_handlers)
    {
        handler->on_finish_parallel_beam_pipeline(ready_queue_high_water, in_flight_chunks_high_water, consumer_stall_time, producer_stall_time);
    }
}

void CompositeEventHandlerImpl::on_solved(const Plan& plan)
{
    for (const auto& handler : m_handlers)
    {
        handler->on_solved(plan);
    }
}

void CompositeEventHandlerImpl::on_unsolvable()
{
    for (const auto& handler : m_handlers)
    {
        handler->on_unsolvable();
    }
}

void CompositeEventHandlerImpl::on_exhausted()
{
    for (const auto& handler : m_handlers)
    {
        handler->on_exhausted();
    }
}

const Statistics& CompositeEventHandlerImpl::get_statistics() const { return m_handlers[m_statistics_source]->get_statistics(); }
}
