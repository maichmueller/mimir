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

#ifndef MIMIR_SEARCH_AXIOM_EVALUATORS_INTERFACE_HPP_
#define MIMIR_SEARCH_AXIOM_EVALUATORS_INTERFACE_HPP_

#include "mimir/common/types_cista.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/search/declarations.hpp"

namespace mimir::search
{

class IParallelAxiomWorkerContext
{
public:
    virtual ~IParallelAxiomWorkerContext() = default;
};

/**
 * Dynamic interface class.
 */
class IAxiomEvaluator
{
public:
    virtual ~IAxiomEvaluator() = default;

    /// @brief Legacy capability bit for the original grounded-only parallel beam path.
    virtual bool supports_parallel_beam() const { return false; }

    /// @brief Return whether this evaluator can evaluate staged successors from worker threads.
    virtual bool supports_parallel_staged_successor_evaluation() const { return false; }

    /// @brief Create a worker-local context reused by a single staged-successor worker thread.
    virtual ParallelAxiomWorkerContext create_parallel_worker_context() const { return nullptr; }

    /// @brief Prepare immutable data needed by worker-local staged-successor evaluation.
    virtual void prepare_parallel_staged_successor_evaluation() {}

    /// @brief Generate all applicable axioms for a given set of ground atoms by running fixed point computation.
    virtual void generate_and_apply_axioms(UnpackedStateImpl& unpacked_state) = 0;

    /// @brief Worker-thread staged-successor axiom evaluation. Only valid if
    /// supports_parallel_staged_successor_evaluation() returns true.
    virtual void generate_and_apply_axioms_parallel(UnpackedStateImpl& unpacked_state, IParallelAxiomWorkerContext& worker_context) const;

    /// @brief Accumulate event handler statistics during search.
    virtual void on_finish_search_layer() = 0;
    virtual void on_end_search() = 0;

    /// @brief Release optional parallel worker memory retained across searches.
    /// If clear_shared_caches is true, also drop shared immutable parallel lookup
    /// tables so they will be rebuilt on the next parallel search.
    virtual void release_parallel_memory(bool clear_shared_caches = false) {}

    /**
     * Getters
     */

    virtual const formalism::Problem& get_problem() const = 0;
};

}

#endif
