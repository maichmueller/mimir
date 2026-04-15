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

#ifndef MIMIR_SEARCH_ALGORITHMS_BRFS_EVENT_HANDLERS_STATISTICS_HPP_
#define MIMIR_SEARCH_ALGORITHMS_BRFS_EVENT_HANDLERS_STATISTICS_HPP_

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <vector>

namespace mimir::search::brfs
{

class IW1IncrementalFirstApplicabilityStatistics
{
private:
    uint64_t m_num_root_actions_fully_enumerated;
    uint64_t m_num_non_root_states_using_incremental_path;
    uint64_t m_num_changed_atoms_processed;
    uint64_t m_num_trigger_records_visited;
    uint64_t m_num_partial_seeds_created;
    uint64_t m_num_ground_actions_returned_by_partial_completion;
    uint64_t m_num_local_duplicate_candidates_removed;
    uint64_t m_num_already_tested_actions_skipped;
    uint64_t m_num_non_root_states_with_zero_returned_actions;
    uint64_t m_trigger_lookup_time_ns;
    uint64_t m_partial_completion_time_ns;
    uint64_t m_debug_crosscheck_time_ns;

public:
    IW1IncrementalFirstApplicabilityStatistics() :
        m_num_root_actions_fully_enumerated(0),
        m_num_non_root_states_using_incremental_path(0),
        m_num_changed_atoms_processed(0),
        m_num_trigger_records_visited(0),
        m_num_partial_seeds_created(0),
        m_num_ground_actions_returned_by_partial_completion(0),
        m_num_local_duplicate_candidates_removed(0),
        m_num_already_tested_actions_skipped(0),
        m_num_non_root_states_with_zero_returned_actions(0),
        m_trigger_lookup_time_ns(0),
        m_partial_completion_time_ns(0),
        m_debug_crosscheck_time_ns(0)
    {
    }

    void set_num_root_actions_fully_enumerated(uint64_t value) { m_num_root_actions_fully_enumerated = value; }
    void set_num_non_root_states_using_incremental_path(uint64_t value) { m_num_non_root_states_using_incremental_path = value; }
    void set_num_changed_atoms_processed(uint64_t value) { m_num_changed_atoms_processed = value; }
    void set_num_trigger_records_visited(uint64_t value) { m_num_trigger_records_visited = value; }
    void set_num_partial_seeds_created(uint64_t value) { m_num_partial_seeds_created = value; }
    void set_num_ground_actions_returned_by_partial_completion(uint64_t value) { m_num_ground_actions_returned_by_partial_completion = value; }
    void set_num_local_duplicate_candidates_removed(uint64_t value) { m_num_local_duplicate_candidates_removed = value; }
    void set_num_already_tested_actions_skipped(uint64_t value) { m_num_already_tested_actions_skipped = value; }
    void set_num_non_root_states_with_zero_returned_actions(uint64_t value) { m_num_non_root_states_with_zero_returned_actions = value; }
    void set_trigger_lookup_time(std::chrono::nanoseconds value) { m_trigger_lookup_time_ns = static_cast<uint64_t>(value.count()); }
    void set_partial_completion_time(std::chrono::nanoseconds value) { m_partial_completion_time_ns = static_cast<uint64_t>(value.count()); }
    void set_debug_crosscheck_time(std::chrono::nanoseconds value) { m_debug_crosscheck_time_ns = static_cast<uint64_t>(value.count()); }

    uint64_t get_num_root_actions_fully_enumerated() const { return m_num_root_actions_fully_enumerated; }
    uint64_t get_num_non_root_states_using_incremental_path() const { return m_num_non_root_states_using_incremental_path; }
    uint64_t get_num_changed_atoms_processed() const { return m_num_changed_atoms_processed; }
    uint64_t get_num_trigger_records_visited() const { return m_num_trigger_records_visited; }
    uint64_t get_num_partial_seeds_created() const { return m_num_partial_seeds_created; }
    uint64_t get_num_ground_actions_returned_by_partial_completion() const { return m_num_ground_actions_returned_by_partial_completion; }
    uint64_t get_num_local_duplicate_candidates_removed() const { return m_num_local_duplicate_candidates_removed; }
    uint64_t get_num_already_tested_actions_skipped() const { return m_num_already_tested_actions_skipped; }
    uint64_t get_num_non_root_states_with_zero_returned_actions() const { return m_num_non_root_states_with_zero_returned_actions; }
    double get_trigger_lookup_time_ms() const { return static_cast<double>(m_trigger_lookup_time_ns) / 1'000'000.0; }
    double get_partial_completion_time_ms() const { return static_cast<double>(m_partial_completion_time_ns) / 1'000'000.0; }
    double get_debug_crosscheck_time_ms() const { return static_cast<double>(m_debug_crosscheck_time_ns) / 1'000'000.0; }
};

class Statistics
{
private:
    uint64_t m_num_generated;
    uint64_t m_num_generated_in_search_tree;
    uint64_t m_num_expanded;
    uint64_t m_num_deadends;
    uint64_t m_num_pruned;
    uint64_t m_num_parallel_beam_chunk_flushes;
    uint64_t m_num_parallel_beam_chunk_tasks_total;
    uint64_t m_max_parallel_beam_chunk_size;
    uint64_t m_parallel_beam_worker_compute_time_ns;
    uint64_t m_parallel_beam_main_thread_merge_time_ns;
    uint64_t m_parallel_beam_main_thread_intern_time_ns;
    uint64_t m_parallel_beam_fluent_slot_time_ns;
    uint64_t m_parallel_beam_numeric_slot_time_ns;
    uint64_t m_parallel_beam_derived_slot_time_ns;
    uint64_t m_parallel_beam_state_lookup_time_ns;
    uint64_t m_parallel_beam_reached_atom_update_time_ns;
    uint64_t m_parallel_beam_ready_queue_high_water;
    uint64_t m_parallel_beam_in_flight_chunks_high_water;
    uint64_t m_parallel_beam_consumer_stall_time_ns;
    uint64_t m_parallel_beam_producer_stall_time_ns;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_search_start_time_point;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_search_end_time_point;

    std::vector<uint64_t> m_num_generated_until_g_value;
    std::vector<uint64_t> m_num_expanded_until_g_value;
    std::vector<uint64_t> m_num_deadends_until_g_value;
    std::vector<uint64_t> m_num_pruned_until_g_value;

    uint64_t m_num_reached_fluent_atoms;
    uint64_t m_num_reached_derived_atoms;

    uint64_t m_num_states;
    uint64_t m_num_nodes;
    uint64_t m_num_actions;
    uint64_t m_num_axioms;
    IW1IncrementalFirstApplicabilityStatistics m_iw1_incremental_first_applicability_statistics;

public:
    Statistics() :
        m_num_generated(0),
        m_num_generated_in_search_tree(0),
        m_num_expanded(0),
        m_num_deadends(0),
        m_num_pruned(0),
        m_num_parallel_beam_chunk_flushes(0),
        m_num_parallel_beam_chunk_tasks_total(0),
        m_max_parallel_beam_chunk_size(0),
        m_parallel_beam_worker_compute_time_ns(0),
        m_parallel_beam_main_thread_merge_time_ns(0),
        m_parallel_beam_main_thread_intern_time_ns(0),
        m_parallel_beam_fluent_slot_time_ns(0),
        m_parallel_beam_numeric_slot_time_ns(0),
        m_parallel_beam_derived_slot_time_ns(0),
        m_parallel_beam_state_lookup_time_ns(0),
        m_parallel_beam_reached_atom_update_time_ns(0),
        m_parallel_beam_ready_queue_high_water(0),
        m_parallel_beam_in_flight_chunks_high_water(0),
        m_parallel_beam_consumer_stall_time_ns(0),
        m_parallel_beam_producer_stall_time_ns(0),
        m_num_generated_until_g_value(),
        m_num_expanded_until_g_value(),
        m_num_deadends_until_g_value(),
        m_num_pruned_until_g_value(),
        m_num_reached_fluent_atoms(0),
        m_num_reached_derived_atoms(0),
        m_num_states(0),
        m_num_nodes(0),
        m_num_actions(0),
        m_num_axioms(0),
        m_iw1_incremental_first_applicability_statistics()
    {
    }

    /**
     * Setters
     */

    /// @brief Store information for the layer
    void on_finish_g_layer()
    {
        m_num_generated_until_g_value.push_back(m_num_generated);
        m_num_expanded_until_g_value.push_back(m_num_expanded);
        m_num_deadends_until_g_value.push_back(m_num_deadends);
        m_num_pruned_until_g_value.push_back(m_num_pruned);
    }

    void increment_num_generated() { ++m_num_generated; }
    void increment_num_generated_in_search_tree() { ++m_num_generated_in_search_tree; }
    void increment_num_expanded() { ++m_num_expanded; }
    void increment_num_deadends() { ++m_num_deadends; }
    void increment_num_pruned() { ++m_num_pruned; }
    void record_parallel_beam_chunk(size_t chunk_size,
                                    std::chrono::nanoseconds worker_compute_time,
                                    std::chrono::nanoseconds main_thread_merge_time,
                                    std::chrono::nanoseconds main_thread_intern_time,
                                    std::chrono::nanoseconds fluent_slot_time,
                                    std::chrono::nanoseconds numeric_slot_time,
                                    std::chrono::nanoseconds derived_slot_time,
                                    std::chrono::nanoseconds state_lookup_time,
                                    std::chrono::nanoseconds reached_atom_update_time)
    {
        ++m_num_parallel_beam_chunk_flushes;
        m_num_parallel_beam_chunk_tasks_total += chunk_size;
        m_max_parallel_beam_chunk_size = std::max<uint64_t>(m_max_parallel_beam_chunk_size, chunk_size);
        m_parallel_beam_worker_compute_time_ns += static_cast<uint64_t>(worker_compute_time.count());
        m_parallel_beam_main_thread_merge_time_ns += static_cast<uint64_t>(main_thread_merge_time.count());
        m_parallel_beam_main_thread_intern_time_ns += static_cast<uint64_t>(main_thread_intern_time.count());
        m_parallel_beam_fluent_slot_time_ns += static_cast<uint64_t>(fluent_slot_time.count());
        m_parallel_beam_numeric_slot_time_ns += static_cast<uint64_t>(numeric_slot_time.count());
        m_parallel_beam_derived_slot_time_ns += static_cast<uint64_t>(derived_slot_time.count());
        m_parallel_beam_state_lookup_time_ns += static_cast<uint64_t>(state_lookup_time.count());
        m_parallel_beam_reached_atom_update_time_ns += static_cast<uint64_t>(reached_atom_update_time.count());
    }
    void record_parallel_beam_pipeline(size_t ready_queue_high_water,
                                       size_t in_flight_chunks_high_water,
                                       std::chrono::nanoseconds consumer_stall_time,
                                       std::chrono::nanoseconds producer_stall_time)
    {
        m_parallel_beam_ready_queue_high_water = std::max<uint64_t>(m_parallel_beam_ready_queue_high_water, ready_queue_high_water);
        m_parallel_beam_in_flight_chunks_high_water = std::max<uint64_t>(m_parallel_beam_in_flight_chunks_high_water, in_flight_chunks_high_water);
        m_parallel_beam_consumer_stall_time_ns += static_cast<uint64_t>(consumer_stall_time.count());
        m_parallel_beam_producer_stall_time_ns += static_cast<uint64_t>(producer_stall_time.count());
    }
    void set_search_start_time_point(std::chrono::time_point<std::chrono::high_resolution_clock> time_point) { m_search_start_time_point = time_point; }
    void set_search_end_time_point(std::chrono::time_point<std::chrono::high_resolution_clock> time_point) { m_search_end_time_point = time_point; }

    void set_num_reached_fluent_atoms(uint64_t num_reached_fluent_atoms) { m_num_reached_fluent_atoms = num_reached_fluent_atoms; }
    void set_num_reached_derived_atoms(uint64_t num_reached_derived_atoms) { m_num_reached_derived_atoms = num_reached_derived_atoms; }

    void set_num_states(uint64_t num_states) { m_num_states = num_states; }
    void set_num_nodes(uint64_t num_nodes) { m_num_nodes = num_nodes; }
    void set_num_actions(uint64_t num_actions) { m_num_actions = num_actions; }
    void set_num_axioms(uint64_t num_axioms) { m_num_axioms = num_axioms; }
    void set_iw1_incremental_first_applicability_statistics(const IW1IncrementalFirstApplicabilityStatistics& statistics)
    {
        m_iw1_incremental_first_applicability_statistics = statistics;
    }

    /**
     * Getters
     */

    uint64_t get_num_generated() const { return m_num_generated; }
    uint64_t get_num_generated_in_search_tree() const { return m_num_generated_in_search_tree; }
    uint64_t get_num_expanded() const { return m_num_expanded; }
    uint64_t get_num_deadends() const { return m_num_deadends; }
    uint64_t get_num_pruned() const { return m_num_pruned; }
    uint64_t get_num_parallel_beam_chunk_flushes() const { return m_num_parallel_beam_chunk_flushes; }
    uint64_t get_num_parallel_beam_chunk_tasks_total() const { return m_num_parallel_beam_chunk_tasks_total; }
    double get_average_parallel_beam_chunk_size() const
    {
        return (m_num_parallel_beam_chunk_flushes == 0) ?
                   0.0 :
                   static_cast<double>(m_num_parallel_beam_chunk_tasks_total) / static_cast<double>(m_num_parallel_beam_chunk_flushes);
    }
    uint64_t get_max_parallel_beam_chunk_size() const { return m_max_parallel_beam_chunk_size; }
    double get_parallel_beam_worker_compute_time_ms() const { return static_cast<double>(m_parallel_beam_worker_compute_time_ns) / 1'000'000.0; }
    double get_parallel_beam_main_thread_merge_time_ms() const
    {
        return static_cast<double>(m_parallel_beam_main_thread_merge_time_ns) / 1'000'000.0;
    }
    double get_parallel_beam_main_thread_intern_time_ms() const
    {
        return static_cast<double>(m_parallel_beam_main_thread_intern_time_ns) / 1'000'000.0;
    }
    double get_parallel_beam_fluent_slot_time_ms() const { return static_cast<double>(m_parallel_beam_fluent_slot_time_ns) / 1'000'000.0; }
    double get_parallel_beam_numeric_slot_time_ms() const { return static_cast<double>(m_parallel_beam_numeric_slot_time_ns) / 1'000'000.0; }
    double get_parallel_beam_derived_slot_time_ms() const { return static_cast<double>(m_parallel_beam_derived_slot_time_ns) / 1'000'000.0; }
    double get_parallel_beam_state_lookup_time_ms() const { return static_cast<double>(m_parallel_beam_state_lookup_time_ns) / 1'000'000.0; }
    double get_parallel_beam_reached_atom_update_time_ms() const
    {
        return static_cast<double>(m_parallel_beam_reached_atom_update_time_ns) / 1'000'000.0;
    }
    uint64_t get_parallel_beam_ready_queue_high_water() const { return m_parallel_beam_ready_queue_high_water; }
    uint64_t get_parallel_beam_in_flight_chunks_high_water() const { return m_parallel_beam_in_flight_chunks_high_water; }
    double get_parallel_beam_consumer_stall_time_ms() const
    {
        return static_cast<double>(m_parallel_beam_consumer_stall_time_ns) / 1'000'000.0;
    }
    double get_parallel_beam_producer_stall_time_ms() const
    {
        return static_cast<double>(m_parallel_beam_producer_stall_time_ns) / 1'000'000.0;
    }

    std::chrono::milliseconds get_search_time_ms() const
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(m_search_end_time_point - m_search_start_time_point);
    }

    std::chrono::milliseconds get_current_search_time_ms() const
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - m_search_start_time_point);
    }

    uint64_t get_num_reached_fluent_atoms() const { return m_num_reached_fluent_atoms; }
    uint64_t get_num_reached_derived_atoms() const { return m_num_reached_derived_atoms; }
    uint64_t get_num_states() const { return m_num_states; }
    uint64_t get_num_nodes() const { return m_num_nodes; }
    uint64_t get_num_actions() const { return m_num_actions; }
    uint64_t get_num_axioms() const { return m_num_axioms; }
    const IW1IncrementalFirstApplicabilityStatistics& get_iw1_incremental_first_applicability_statistics() const
    {
        return m_iw1_incremental_first_applicability_statistics;
    }

    const std::vector<uint64_t>& get_num_generated_until_g_value() const { return m_num_generated_until_g_value; }
    const std::vector<uint64_t>& get_num_expanded_until_g_value() const { return m_num_expanded_until_g_value; }
    const std::vector<uint64_t>& get_num_deadends_until_g_value() const { return m_num_deadends_until_g_value; }
    const std::vector<uint64_t>& get_num_pruned_until_g_value() const { return m_num_pruned_until_g_value; }
};

/**
 * Types
 */

using StatisticsList = std::vector<Statistics>;

}

#endif
