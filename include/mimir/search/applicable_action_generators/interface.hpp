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

#include <chrono>

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

struct ParallelRelaxedBeamSuccessorCandidate
{
    const State* parent_state = nullptr;
    formalism::Action action_schema = nullptr;
    IndexList binding_object_indices;
    FlatBitset fluent_atoms;
    FlatBitset derived_atoms;
    iw::AtomIndexList fluent_atom_indices;
    iw::AtomIndexList derived_atom_indices;
    FlatDoubleList fluent_numeric_variables;
    ContinuousCost action_cost = 0;
    ContinuousCost successor_metric_value = 0;
    DiscreteCost successor_g_value = 0;
    ContinuousCost score = 0;
    uint64_t generation_sequence = 0;
    uint64_t tie_token = 0;
};

struct ParallelRelaxedBeamSuccessorGenerationResult
{
    std::vector<ParallelRelaxedBeamSuccessorCandidate> candidates;
    std::chrono::nanoseconds worker_compute_time = std::chrono::nanoseconds::zero();
    size_t num_scored_candidates = 0;
};

struct PartialGroundActionSeed
{
    formalism::Action action_schema = nullptr;
    IndexList bound_parameter_object_indices;
    std::vector<uint8_t> parameter_is_bound;
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

    /// @brief Return whether this generator can complete a partially bound lifted action
    /// into fully applicable ground actions in the given state.
    virtual bool supports_partial_binding_completion() const { return false; }

    /// @brief Complete a partially bound lifted action into applicable ground actions.
    /// Only valid if supports_partial_binding_completion() returns true.
    virtual void create_applicable_actions_from_partial_binding(const State& state,
                                                                const PartialGroundActionSeed& seed,
                                                                std::vector<formalism::GroundAction>& out_actions);

    /// @brief Deterministic parallel applicable-action enumeration. Only valid if
    /// supports_parallel_applicable_action_generation() returns true.
    virtual std::vector<formalism::GroundAction> create_applicable_action_list_parallel(const State& state, BS::thread_pool& thread_pool);

    /// @brief Return whether this generator can directly produce worker-scored staged
    /// successor candidates for relaxed SURVIVORS_ONLY beam search.
    virtual bool supports_parallel_relaxed_beam_successor_generation() const { return false; }

    /// @brief Generate relaxed-beam successor candidates in workers. The main thread only
    /// sees the reduced candidate set and materializes canonical states/actions afterward.
    virtual ParallelRelaxedBeamSuccessorGenerationResult create_relaxed_parallel_beam_successor_candidates(
        const State& state,
        ContinuousCost state_metric_value,
        DiscreteCost successor_g_value,
        BS::thread_pool& thread_pool,
        StateRepositoryImpl& state_repository,
        const PruningStrategy& pruning_strategy,
        BeamNoveltyMode beam_novelty_mode,
        const LayerOrderingStrategy& layer_ordering_strategy,
        uint32_t beam_width,
        bool iw1_precheck_add_effect_novelty,
        bool randomize_equal_score_ties,
        uint64_t equal_score_tie_seed);

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
