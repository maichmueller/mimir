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

#ifndef MIMIR_SEARCH_APPLICABLE_ACTION_GENERATORS_LIFTED_HPP_
#define MIMIR_SEARCH_APPLICABLE_ACTION_GENERATORS_LIFTED_HPP_

#include "mimir/formalism/assignment_set.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/formalism/problem_details.hpp"
#include "mimir/search/applicable_action_generators/interface.hpp"
#include "mimir/search/applicable_action_generators/lifted/kpkc/event_handlers/statistics.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/search/satisficing_binding_generators/action.hpp"
#include "mimir/search/search_context.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace mimir::search
{
/// @brief `KPKCLiftedApplicableActionGeneratorImpl` implements lifted applicable action generation
/// using maximum clique enumeration by Stahlberg (ECAI2023).
/// Source: https://mrlab.ai/papers/stahlberg-ecai2023.pdf
class KPKCLiftedApplicableActionGeneratorImpl : public IApplicableActionGenerator
{
public:
    using Statistics = applicable_action_generator::lifted::kpkc::Statistics;

    struct GenerationStatistics
    {
        uint64_t num_generation_calls = 0;
        uint64_t total_generation_time_ns = 0;
        uint64_t total_dynamic_assignment_initialization_time_ns = 0;
        uint64_t total_symmetry_setup_time_ns = 0;

        void record_generation(uint64_t generation_time_ns, uint64_t dynamic_assignment_initialization_time_ns, uint64_t symmetry_setup_time_ns)
        {
            ++num_generation_calls;
            total_generation_time_ns += generation_time_ns;
            total_dynamic_assignment_initialization_time_ns += dynamic_assignment_initialization_time_ns;
            total_symmetry_setup_time_ns += symmetry_setup_time_ns;
        }

        double get_total_generation_time_ms() const { return static_cast<double>(total_generation_time_ns) / 1'000'000.0; }
        double get_total_dynamic_assignment_initialization_time_ms() const
        {
            return static_cast<double>(total_dynamic_assignment_initialization_time_ns) / 1'000'000.0;
        }
        double get_total_symmetry_setup_time_ms() const { return static_cast<double>(total_symmetry_setup_time_ns) / 1'000'000.0; }
    };

    using IEventHandler = applicable_action_generator::lifted::kpkc::IEventHandler;
    using EventHandler = applicable_action_generator::lifted::kpkc::EventHandler;

    using DebugEventHandlerImpl = applicable_action_generator::lifted::kpkc::DebugEventHandlerImpl;
    using DebugEventHandler = applicable_action_generator::lifted::kpkc::DebugEventHandler;

    using DefaultEventHandlerImpl = applicable_action_generator::lifted::kpkc::DefaultEventHandlerImpl;
    using DefaultEventHandler = applicable_action_generator::lifted::kpkc::DefaultEventHandler;

    KPKCLiftedApplicableActionGeneratorImpl(formalism::Problem problem,
                                            const SearchContextImpl::LiftedOptions::KPKCOptions& options = SearchContextImpl::LiftedOptions::KPKCOptions(),
                                            EventHandler event_handler = nullptr,
                                            satisficing_binding_generator::EventHandler binding_event_handler = nullptr);

    static KPKCLiftedApplicableActionGenerator
    create(formalism::Problem problem,
           const SearchContextImpl::LiftedOptions::KPKCOptions& options = SearchContextImpl::LiftedOptions::KPKCOptions(),
           EventHandler event_handler = nullptr,
           satisficing_binding_generator::EventHandler binding_event_handler = nullptr);

    // Uncopyable
    KPKCLiftedApplicableActionGeneratorImpl(const KPKCLiftedApplicableActionGeneratorImpl& other) = delete;
    KPKCLiftedApplicableActionGeneratorImpl& operator=(const KPKCLiftedApplicableActionGeneratorImpl& other) = delete;
    // Unmovable
    KPKCLiftedApplicableActionGeneratorImpl(KPKCLiftedApplicableActionGeneratorImpl&& other) = delete;
    KPKCLiftedApplicableActionGeneratorImpl& operator=(KPKCLiftedApplicableActionGeneratorImpl&& other) = delete;

    bool supports_parallel_applicable_action_generation() const override;
    bool supports_parallel_relaxed_beam_successor_generation() const override;
    ParallelApplicableActionGeneratorWorkerContext create_parallel_worker_context() const override;
    mimir::generator<formalism::GroundAction> create_applicable_action_generator(const State& state) override;
    bool supports_partial_binding_completion() const override;
    void create_applicable_actions_from_partial_binding(const State& state,
                                                        const PartialGroundActionSeed& seed,
                                                        std::vector<formalism::GroundAction>& out_actions) override;
    std::vector<formalism::GroundAction> create_applicable_action_list_parallel(const State& state, BS::thread_pool& thread_pool) override;
    ParallelRelaxedBeamSuccessorGenerationResult create_relaxed_parallel_beam_successor_candidates(
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
        uint64_t equal_score_tie_seed) override;

    void on_finish_search_layer() override;
    void on_end_search() override;
    void release_parallel_memory(bool clear_shared_caches = false) override;

    /**
     * Getters
     */

    const formalism::Problem& get_problem() const override;
    const GenerationStatistics& get_generation_statistics() const;

private:
    struct ParallelGroundLookupTables;

    void prepare_parallel_applicable_action_generation() const;
    std::vector<ParallelApplicableActionGeneratorWorkerContext>& get_parallel_worker_contexts(size_t thread_count);

    formalism::Problem m_problem;
    SearchContextImpl::LiftedOptions::KPKCOptions m_options;
    EventHandler m_event_handler;
    satisficing_binding_generator::EventHandler m_binding_event_handler;

    ActionSatisficingBindingGeneratorList m_action_grounding_data;

    formalism::DynamicAssignmentSets m_dynamic_assignment_sets;
    GenerationStatistics m_generation_statistics;
    mutable std::mutex m_parallel_lookup_tables_mutex;
    mutable std::shared_ptr<const ParallelGroundLookupTables> m_parallel_lookup_tables;
    std::vector<ParallelApplicableActionGeneratorWorkerContext> m_parallel_worker_contexts;
};

}  // namespace mimir

#endif
