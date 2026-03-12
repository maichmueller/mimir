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

#ifndef MIMIR_SEARCH_APPLICABLE_ACTION_GENERATORS_INTERFACE_HPP_
#define MIMIR_SEARCH_APPLICABLE_ACTION_GENERATORS_INTERFACE_HPP_

#include "mimir/algorithms/generator.hpp"
#include "mimir/common/types_cista.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/search/declarations.hpp"

namespace BS
{
class thread_pool;
}

namespace mimir::search
{

class IParallelApplicableActionGeneratorWorkerContext
{
public:
    virtual ~IParallelApplicableActionGeneratorWorkerContext() = default;
};

/**
 * Dynamic interface class.
 */
class IApplicableActionGenerator
{
public:
    virtual ~IApplicableActionGenerator() = default;

    /// @brief Return whether this generator can participate in the grounded-only
    /// parallel beam path without shared mutable search-layer state.
    virtual bool supports_parallel_beam() const { return false; }

    /// @brief Return whether this generator can enumerate applicable actions from worker threads
    /// while keeping main-thread grounding and final action order deterministic.
    virtual bool supports_parallel_applicable_action_generation() const { return false; }

    /// @brief Create a worker-local context reused by a single parallel applicable-action worker thread.
    virtual ParallelApplicableActionGeneratorWorkerContext create_parallel_worker_context() const { return nullptr; }

    /// @brief Generate all applicable actions for a given state.
    virtual mimir::generator<formalism::GroundAction> create_applicable_action_generator(const State& state) = 0;

    /// @brief Deterministic parallel applicable-action enumeration. Only valid if
    /// supports_parallel_applicable_action_generation() returns true.
    virtual std::vector<formalism::GroundAction> create_applicable_action_list_parallel(const State& state, BS::thread_pool& thread_pool);

    /// @brief Accumulate event handler statistics during search.
    virtual void on_finish_search_layer() = 0;
    virtual void on_end_search() = 0;

    /**
     * Getters
     */

    virtual const formalism::Problem& get_problem() const = 0;
};

}

#endif
