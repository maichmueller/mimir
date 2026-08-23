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

#ifndef MIMIR_SEARCH_ALGORITHMS_BRFS_EVENT_HANDLERS_COMPOSITE_HPP_
#define MIMIR_SEARCH_ALGORITHMS_BRFS_EVENT_HANDLERS_COMPOSITE_HPP_

#include "mimir/search/algorithms/brfs/event_handlers/interface.hpp"

#include <cstddef>
#include <vector>

namespace mimir::search::brfs
{

/// @brief Delivers every event to several handlers, so a search can be observed natively *and* call
/// back into Python without either observer having to be given up.
///
/// It is not itself an `EventHandlerBase` and keeps no counters: each child counts what it sees, so
/// fanning an event out cannot double-count anything.
///
/// Capabilities are combined the only way that keeps every child correct:
///   - novelty witnesses are computed if *any* child asks for them, since a child that does not want
///     them simply ignores the extra event;
///   - the payloadless fast path is taken only if *every* child can work without payload, since one
///     child that needs the parent and action would otherwise silently lose transitions.
class CompositeEventHandlerImpl : public IEventHandler
{
public:
    /// @param handlers the observers, in the order they should be notified.
    /// @param statistics_source index of the child whose statistics `get_statistics` reports. The
    ///        interface returns a reference to one `Statistics`, and a composite has no counters of
    ///        its own, so one child has to be nominated.
    CompositeEventHandlerImpl(std::vector<EventHandler> handlers, size_t statistics_source);

    static EventHandler create(std::vector<EventHandler> handlers, size_t statistics_source);

    void on_expand_state(const State& state) override;
    void on_expand_goal_state(const State& state) override;
    void on_generate_state(const State& state, formalism::GroundAction action, ContinuousCost action_cost, const State& successor_state) override;

    bool supports_novel_witness_events() const override;
    void on_generate_state_with_novel_witness(const State& state,
                                              formalism::GroundAction action,
                                              ContinuousCost action_cost,
                                              const State& successor_state,
                                              const iw::AtomIndexList& novel_fluent_atom_indices) override;

    bool supports_payloadless_generated_state_events() const override;
    void on_generate_state_without_payload() override;
    void on_generate_state_in_search_tree_without_payload() override;
    void on_generate_state_not_in_search_tree_without_payload() override;

    void on_generate_state_in_search_tree(const State& state, formalism::GroundAction action, ContinuousCost action_cost, const State& successor_state) override;
    void
    on_generate_state_not_in_search_tree(const State& state, formalism::GroundAction action, ContinuousCost action_cost, const State& successor_state) override;

    void on_finish_g_layer(DiscreteCost g_value) override;
    void on_start_search(const State& start_state) override;
    void on_end_search(uint64_t num_reached_fluent_atoms,
                       uint64_t num_reached_derived_atoms,
                       uint64_t num_states,
                       uint64_t num_nodes,
                       uint64_t num_actions,
                       uint64_t num_axioms) override;

    void on_finish_iw1_incremental_first_applicability(
        const IW1IncrementalFirstApplicabilityStatistics& iw1_incremental_first_applicability_statistics) override;

    void on_finish_parallel_beam_chunk(size_t chunk_size,
                                       std::chrono::nanoseconds worker_compute_time,
                                       std::chrono::nanoseconds main_thread_merge_time,
                                       std::chrono::nanoseconds main_thread_intern_time,
                                       std::chrono::nanoseconds fluent_slot_time,
                                       std::chrono::nanoseconds numeric_slot_time,
                                       std::chrono::nanoseconds derived_slot_time,
                                       std::chrono::nanoseconds state_lookup_time,
                                       std::chrono::nanoseconds reached_atom_update_time) override;

    void on_finish_parallel_beam_pipeline(size_t ready_queue_high_water,
                                          size_t in_flight_chunks_high_water,
                                          std::chrono::nanoseconds consumer_stall_time,
                                          std::chrono::nanoseconds producer_stall_time) override;

    void on_solved(const Plan& plan) override;
    void on_unsolvable() override;
    void on_exhausted() override;

    const Statistics& get_statistics() const override;

    const std::vector<EventHandler>& get_handlers() const { return m_handlers; }

private:
    std::vector<EventHandler> m_handlers;
    size_t m_statistics_source;
};

}

#endif
