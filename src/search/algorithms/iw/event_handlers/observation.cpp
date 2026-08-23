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

#include "mimir/search/algorithms/iw/event_handlers/observation.hpp"

#include <stdexcept>

using namespace mimir::formalism;

namespace mimir::search::iw
{
ObservationEventHandlerImpl::ObservationEventHandlerImpl(Problem problem, brfs::ObservationEventHandler brfs_observation_handler) :
    EventHandlerBase<ObservationEventHandlerImpl>(std::move(problem), true),
    m_brfs_observation_handler(std::move(brfs_observation_handler)),
    m_observation(),
    m_current_arity(0)
{
    if (!m_brfs_observation_handler)
    {
        throw std::invalid_argument("iw::ObservationEventHandlerImpl: a BrFS observation handler is required.");
    }
}

ObservationEventHandler ObservationEventHandlerImpl::create(Problem problem, brfs::ObservationEventHandler brfs_observation_handler)
{
    return std::make_shared<ObservationEventHandlerImpl>(std::move(problem), std::move(brfs_observation_handler));
}

void ObservationEventHandlerImpl::on_start_search(const State& initial_state)
{
    EventHandlerBase<ObservationEventHandlerImpl>::on_start_search(initial_state);

    m_observation.clear();
    m_current_arity = 0;
}

void ObservationEventHandlerImpl::on_start_arity_search(const State& initial_state, size_t arity)
{
    EventHandlerBase<ObservationEventHandlerImpl>::on_start_arity_search(initial_state, arity);

    m_current_arity = arity;
}

void ObservationEventHandlerImpl::on_end_arity_search(const brfs::Statistics& brfs_statistics)
{
    EventHandlerBase<ObservationEventHandlerImpl>::on_end_arity_search(brfs_statistics);

    /* Taking the observation both hands this pass its own copy and leaves the BrFS handler empty for
       the next one. Optimized IW(1) reports a width-0 pass that runs no search at all, so what is
       taken there is the empty observation the handler starts with -- which is exactly the
       placeholder that keeps entry positions lined up with widths. */
    m_observation.push_back(ArityObservation(m_current_arity, brfs_statistics, m_brfs_observation_handler->take_observation()));
}

Observation ObservationEventHandlerImpl::take_observation()
{
    auto observation = std::move(m_observation);
    m_observation = Observation();
    return observation;
}
}
