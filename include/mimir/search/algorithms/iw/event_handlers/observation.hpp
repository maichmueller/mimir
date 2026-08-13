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

#ifndef MIMIR_SEARCH_ALGORITHMS_IW_EVENT_HANDLERS_OBSERVATION_HPP_
#define MIMIR_SEARCH_ALGORITHMS_IW_EVENT_HANDLERS_OBSERVATION_HPP_

#include "mimir/search/algorithms/brfs/event_handlers/observation.hpp"
#include "mimir/search/algorithms/iw/event_handlers/interface.hpp"

#include <cstddef>
#include <vector>

namespace mimir::search::iw
{

/// @brief What one arity pass of an IW search produced.
///
/// Each pass is its own BrFS run over its own novelty table, so its tree has its own root and its own
/// node-index namespace: node 3 of the width-1 tree and node 3 of the width-2 tree are unrelated.
/// Merging them would invent edges no search ever took.
struct ArityObservation
{
    size_t arity;
    brfs::Statistics statistics;
    brfs::Observation observation;

    ArityObservation(size_t arity, brfs::Statistics statistics, brfs::Observation observation) :
        arity(arity),
        statistics(std::move(statistics)),
        observation(std::move(observation))
    {
    }

    ArityObservation(const ArityObservation&) = delete;
    ArityObservation& operator=(const ArityObservation&) = delete;
    ArityObservation(ArityObservation&&) = default;
    ArityObservation& operator=(ArityObservation&&) = default;
};

/// @brief One entry per attempted arity, in the order the arities were attempted.
///
/// Optimized IW(1) reports a width-0 pass that never runs a search; it appears here with empty
/// statistics and an empty observation rather than being dropped, so an entry's position keeps
/// matching the width it stands for.
class Observation
{
public:
    Observation() = default;
    Observation(const Observation&) = delete;
    Observation& operator=(const Observation&) = delete;
    Observation(Observation&&) = default;
    Observation& operator=(Observation&&) = default;

    void clear() { m_by_arity.clear(); }
    void push_back(ArityObservation arity_observation) { m_by_arity.push_back(std::move(arity_observation)); }

    const std::vector<ArityObservation>& get_by_arity() const { return m_by_arity; }
    size_t get_num_arities() const { return m_by_arity.size(); }

private:
    std::vector<ArityObservation> m_by_arity;
};

/**
 * Implementation class
 */

/// @brief Collects one `brfs::Observation` per arity pass by taking the BrFS observation handler's
/// output as each pass ends.
///
/// It has to hold the same handler the IW search was given as `brfs_event_handler`, because that
/// handler is the only thing that saw the pass, and it clears itself when the next pass starts.
/// `on_end_arity_search` fires after every pass whatever its status, so a pass that timed out still
/// contributes what it managed to observe and the passes before it are kept intact.
class ObservationEventHandlerImpl : public EventHandlerBase<ObservationEventHandlerImpl>
{
private:
    friend class EventHandlerBase<ObservationEventHandlerImpl>;

    /* Always quiet, but the base is a template and instantiates these calls regardless. */
    void on_start_search_impl(const State&) const {}
    void on_start_arity_search_impl(const State&, size_t) const {}
    void on_end_arity_search_impl(const brfs::Statistics&) const {}
    void on_end_search_impl() const {}
    void on_solved_impl(const Plan&) const {}
    void on_unsolvable_impl() const {}
    void on_exhausted_impl() const {}

    brfs::ObservationEventHandler m_brfs_observation_handler;
    Observation m_observation;
    size_t m_current_arity;

public:
    /// @param brfs_observation_handler the very handler passed to `iw::Options::brfs_event_handler`
    ///        (or wrapped in the composite that was passed there).
    ObservationEventHandlerImpl(formalism::Problem problem, brfs::ObservationEventHandler brfs_observation_handler);

    static ObservationEventHandler create(formalism::Problem problem, brfs::ObservationEventHandler brfs_observation_handler);

    void on_start_search(const State& initial_state) override;
    void on_start_arity_search(const State& initial_state, size_t arity) override;
    void on_end_arity_search(const brfs::Statistics& brfs_statistics) override;

    const Observation& get_observation() const { return m_observation; }
    Observation take_observation();
};

}

#endif
