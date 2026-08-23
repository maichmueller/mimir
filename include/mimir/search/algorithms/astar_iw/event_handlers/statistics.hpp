#ifndef MIMIR_SEARCH_ALGORITHMS_ASTAR_IW_EVENT_HANDLERS_STATISTICS_HPP_
#define MIMIR_SEARCH_ALGORITHMS_ASTAR_IW_EVENT_HANDLERS_STATISTICS_HPP_

#include "mimir/common/declarations.hpp"
#include "mimir/search/declarations.hpp"

#include <chrono>
#include <cstdint>

namespace mimir::search::astar_iw
{

class Statistics
{
private:
    uint64_t m_num_generated = 0;
    uint64_t m_num_expanded = 0;
    uint64_t m_num_reopened = 0;
    uint64_t m_num_deadends = 0;
    uint64_t m_num_novelty_rejected = 0;
    uint64_t m_num_stale_g_discarded = 0;
    uint64_t m_num_stale_novelty_discarded = 0;
    uint64_t m_num_states = 0;
    uint64_t m_num_nodes = 0;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start_time;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_end_time;

public:
    void reset() { *this = Statistics(); }
    void start() { m_start_time = std::chrono::high_resolution_clock::now(); }
    void stop() { m_end_time = std::chrono::high_resolution_clock::now(); }
    void increment_num_generated() { ++m_num_generated; }
    void increment_num_expanded() { ++m_num_expanded; }
    void increment_num_reopened() { ++m_num_reopened; }
    void increment_num_deadends() { ++m_num_deadends; }
    void increment_num_novelty_rejected() { ++m_num_novelty_rejected; }
    void increment_num_stale_g_discarded() { ++m_num_stale_g_discarded; }
    void increment_num_stale_novelty_discarded() { ++m_num_stale_novelty_discarded; }
    void set_num_states(uint64_t value) { m_num_states = value; }
    void set_num_nodes(uint64_t value) { m_num_nodes = value; }

    uint64_t get_num_generated() const { return m_num_generated; }
    uint64_t get_num_expanded() const { return m_num_expanded; }
    uint64_t get_num_reopened() const { return m_num_reopened; }
    uint64_t get_num_deadends() const { return m_num_deadends; }
    uint64_t get_num_novelty_rejected() const { return m_num_novelty_rejected; }
    uint64_t get_num_stale_g_discarded() const { return m_num_stale_g_discarded; }
    uint64_t get_num_stale_novelty_discarded() const { return m_num_stale_novelty_discarded; }
    uint64_t get_num_states() const { return m_num_states; }
    uint64_t get_num_nodes() const { return m_num_nodes; }
    std::chrono::milliseconds get_search_time_ms() const { return std::chrono::duration_cast<std::chrono::milliseconds>(m_end_time - m_start_time); }
};

}

#endif
