#include "mimir/formalism/problem.hpp"
#include "mimir/search/algorithms/brfs/event_handlers.hpp"
#include "mimir/search/algorithms/iw.hpp"
#include "mimir/search/algorithms/iw/event_handlers.hpp"
#include "mimir/search/algorithms/iw/pruning_strategy.hpp"
#include "mimir/search/algorithms/strategies/layer_ordering_strategy.hpp"
#include "mimir/search/applicable_action_generators.hpp"
#include "mimir/search/axiom_evaluators.hpp"
#include "mimir/search/grounders.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <chrono>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <limits>

using namespace mimir::formalism;
using namespace mimir::search;

namespace
{
struct BenchmarkResult
{
    SearchStatus status;
    size_t generated;
    size_t novel_generated;
    double wall_time_ms;
    double lifted_action_generation_time_ms;
    double lifted_dynamic_assignment_initialization_time_ms;
    double lifted_symmetry_setup_time_ms;
    uint64_t parallel_chunk_flushes;
    uint64_t parallel_chunk_tasks_total;
    uint64_t max_parallel_chunk_size;
    double average_parallel_chunk_size;
    double parallel_worker_compute_time_ms;
    double parallel_main_thread_merge_time_ms;
    double parallel_main_thread_intern_time_ms;
    double parallel_fluent_slot_time_ms;
    double parallel_numeric_slot_time_ms;
    double parallel_derived_slot_time_ms;
    double parallel_state_lookup_time_ms;
    double parallel_reached_atom_update_time_ms;
    uint64_t parallel_ready_queue_high_water;
    uint64_t parallel_in_flight_chunks_high_water;
    double parallel_consumer_stall_time_ms;
    double parallel_producer_stall_time_ms;
    uint64_t incremental_root_actions_fully_enumerated;
    uint64_t incremental_non_root_states_using_incremental_path;
    uint64_t incremental_changed_atoms_processed;
    uint64_t incremental_trigger_records_visited;
    uint64_t incremental_partial_seeds_created;
    uint64_t incremental_ground_actions_returned_by_partial_completion;
    uint64_t incremental_local_duplicate_candidates_removed;
    uint64_t incremental_already_tested_actions_skipped;
    uint64_t incremental_non_root_states_with_zero_returned_actions;
    double incremental_trigger_lookup_time_ms;
    double incremental_partial_completion_time_ms;
    double incremental_debug_crosscheck_time_ms;
};

enum class IW1ActionSelectionMode
{
    OFF,
    ACTION_FIRST,
    ATOM_FIRST,
    BOTH,
    ALL
};

enum class IW1NoveltyBasis
{
    CLASSICAL,
    PROJECTIVE,
    PROJECTIVE_TYPED,
    ABSTRACTED_BASE,
    ABSTRACTED_TYPED,
    BOTH
};

SearchContextImpl::Options parse_search_mode(const std::string& mode)
{
    if (mode == "grounded")
    {
        return SearchContextImpl::Options(SearchContextImpl::GroundedOptions());
    }
    if (mode == "lifted")
    {
        return SearchContextImpl::Options(SearchContextImpl::LiftedOptions(SearchContextImpl::LiftedOptions::KPKCOptions()));
    }
    if (mode == "lifted_symmetry_pruning")
    {
        return SearchContextImpl::Options(
            SearchContextImpl::LiftedOptions(SearchContextImpl::LiftedOptions::KPKCOptions(SearchContextImpl::SymmetryPruning::GI)));
    }
    if (mode == "lifted_exhaustive")
    {
        return SearchContextImpl::Options(SearchContextImpl::LiftedOptions(SearchContextImpl::LiftedOptions::ExhaustiveOptions()));
    }

    throw std::invalid_argument("Expected search mode to be 'grounded', 'lifted', 'lifted_symmetry_pruning', or 'lifted_exhaustive'.");
}

IW1ActionSelectionMode parse_iw1_action_selection_mode(const std::string& mode)
{
    if (mode == "off")
    {
        return IW1ActionSelectionMode::OFF;
    }
    if (mode == "action_first")
    {
        return IW1ActionSelectionMode::ACTION_FIRST;
    }
    if (mode == "atom_first")
    {
        return IW1ActionSelectionMode::ATOM_FIRST;
    }
    if (mode == "both")
    {
        return IW1ActionSelectionMode::BOTH;
    }
    if (mode == "all")
    {
        return IW1ActionSelectionMode::ALL;
    }

    throw std::invalid_argument("Expected IW1 action selection mode to be 'off', 'action_first', 'atom_first', 'both', or 'all'.");
}

const char* to_string(IW1ActionSelectionMode mode)
{
    switch (mode)
    {
        case IW1ActionSelectionMode::OFF:
            return "off";
        case IW1ActionSelectionMode::ACTION_FIRST:
            return "action_first";
        case IW1ActionSelectionMode::ATOM_FIRST:
            return "atom_first";
        case IW1ActionSelectionMode::BOTH:
            return "both";
        case IW1ActionSelectionMode::ALL:
            return "all";
    }
    return "unknown";
}

IW1NoveltyBasis parse_iw1_novelty_basis(const std::string& basis)
{
    if (basis == "classical")
    {
        return IW1NoveltyBasis::CLASSICAL;
    }
    if (basis == "projective")
    {
        return IW1NoveltyBasis::PROJECTIVE;
    }
    if (basis == "projective_typed")
    {
        return IW1NoveltyBasis::PROJECTIVE_TYPED;
    }
    if (basis == "abstracted_base")
    {
        return IW1NoveltyBasis::ABSTRACTED_BASE;
    }
    if (basis == "abstracted" || basis == "abstracted_typed")
    {
        return IW1NoveltyBasis::ABSTRACTED_TYPED;
    }
    if (basis == "both")
    {
        return IW1NoveltyBasis::BOTH;
    }

    throw std::invalid_argument(
        "Expected IW1 novelty basis to be 'classical', 'projective', 'projective_typed', 'abstracted_base', 'abstracted_typed', 'abstracted', or 'both'.");
}

const char* to_string(IW1NoveltyBasis basis)
{
    switch (basis)
    {
        case IW1NoveltyBasis::CLASSICAL:
            return "classical";
        case IW1NoveltyBasis::PROJECTIVE:
            return "projective";
        case IW1NoveltyBasis::PROJECTIVE_TYPED:
            return "projective_typed";
        case IW1NoveltyBasis::ABSTRACTED_BASE:
            return "abstracted_base";
        case IW1NoveltyBasis::ABSTRACTED_TYPED:
            return "abstracted_typed";
        case IW1NoveltyBasis::BOTH:
            return "both";
    }
    return "unknown";
}

BenchmarkResult run_once(const std::filesystem::path& domain_file,
                         const std::filesystem::path& problem_file,
                         const SearchContextImpl::Options& search_context_options,
                         size_t max_arity,
                         size_t beam_width,
                         BeamNoveltyMode beam_novelty_mode,
                         bool relaxed_survivors_only_beam,
                         uint32_t num_threads,
                         uint32_t chunk_size,
                         bool plain_brfs,
                         bool iw1_precheck_add_effect_novelty,
                         bool iw1_atom_first_mode,
                         double iw1_atom_first_ratio,
                         bool iw1_incremental_first_applicability,
                         bool iw1_incremental_first_applicability_debug_crosscheck,
                         IW1NoveltyBasis iw1_novelty_basis,
                         bool projective_keep_depth_one_novel)
{
    auto search_context = SearchContextImpl::create(domain_file, problem_file, search_context_options);
    const auto problem = search_context->get_problem();
    auto brfs_event_handler = brfs::DefaultEventHandlerImpl::create(problem, true);
    auto iw_event_handler = iw::DefaultEventHandlerImpl::create(problem, true);

    auto status = SearchStatus::FAILED;
    size_t generated = 0;
    size_t novel_generated = 0;
    uint64_t parallel_chunk_flushes = 0;
    uint64_t parallel_chunk_tasks_total = 0;
    uint64_t max_parallel_chunk_size = 0;
    double parallel_worker_compute_time_ms = 0.0;
    double parallel_main_thread_merge_time_ms = 0.0;
    double parallel_main_thread_intern_time_ms = 0.0;
    double parallel_fluent_slot_time_ms = 0.0;
    double parallel_numeric_slot_time_ms = 0.0;
    double parallel_derived_slot_time_ms = 0.0;
    double parallel_state_lookup_time_ms = 0.0;
    double parallel_reached_atom_update_time_ms = 0.0;
    uint64_t parallel_ready_queue_high_water = 0;
    uint64_t parallel_in_flight_chunks_high_water = 0;
    double parallel_consumer_stall_time_ms = 0.0;
    double parallel_producer_stall_time_ms = 0.0;
    uint64_t incremental_root_actions_fully_enumerated = 0;
    uint64_t incremental_non_root_states_using_incremental_path = 0;
    uint64_t incremental_changed_atoms_processed = 0;
    uint64_t incremental_trigger_records_visited = 0;
    uint64_t incremental_partial_seeds_created = 0;
    uint64_t incremental_ground_actions_returned_by_partial_completion = 0;
    uint64_t incremental_local_duplicate_candidates_removed = 0;
    uint64_t incremental_already_tested_actions_skipped = 0;
    uint64_t incremental_non_root_states_with_zero_returned_actions = 0;
    double incremental_trigger_lookup_time_ms = 0.0;
    double incremental_partial_completion_time_ms = 0.0;
    double incremental_debug_crosscheck_time_ms = 0.0;

    const auto wall_start = std::chrono::steady_clock::now();
    if (iw1_novelty_basis == IW1NoveltyBasis::CLASSICAL)
    {
        if (plain_brfs && max_arity == 1)
        {
            const auto& ground_fluent_atom_repository =
                boost::hana::at_key(search_context->get_problem()->get_repositories().get_hana_repositories(), boost::hana::type<GroundAtomImpl<FluentTag>> {});

            auto options = brfs::Options();
            options.event_handler = brfs_event_handler;
            options.pruning_strategy = iw::ArityKNoveltyPruningStrategyImpl::create(1, ground_fluent_atom_repository.size());
            options.beam_width = static_cast<uint32_t>(beam_width);
            options.beam_novelty_mode = beam_novelty_mode;
            options.relaxed_survivors_only_beam = relaxed_survivors_only_beam;
            options.parallel_beam_num_threads = num_threads;
            options.parallel_beam_chunk_size = chunk_size;
            options.iw1_precheck_add_effect_novelty = iw1_precheck_add_effect_novelty;
            options.iw1_atom_first_mode = iw1_atom_first_mode;
            options.iw1_atom_first_ratio = iw1_atom_first_ratio;
            options.iw1_incremental_first_applicability = iw1_incremental_first_applicability;
            options.iw1_incremental_first_applicability_debug_crosscheck = iw1_incremental_first_applicability_debug_crosscheck;

            const auto result = brfs::find_solution(search_context, options);
            status = result.status;

            const auto& brfs_statistics = brfs_event_handler->get_statistics();
            generated = brfs_statistics.get_num_generated();
            novel_generated = brfs_statistics.get_num_generated_in_search_tree();
            parallel_chunk_flushes = brfs_statistics.get_num_parallel_beam_chunk_flushes();
            parallel_chunk_tasks_total = brfs_statistics.get_num_parallel_beam_chunk_tasks_total();
            max_parallel_chunk_size = brfs_statistics.get_max_parallel_beam_chunk_size();
            parallel_worker_compute_time_ms = brfs_statistics.get_parallel_beam_worker_compute_time_ms();
            parallel_main_thread_merge_time_ms = brfs_statistics.get_parallel_beam_main_thread_merge_time_ms();
            parallel_main_thread_intern_time_ms = brfs_statistics.get_parallel_beam_main_thread_intern_time_ms();
            parallel_fluent_slot_time_ms = brfs_statistics.get_parallel_beam_fluent_slot_time_ms();
            parallel_numeric_slot_time_ms = brfs_statistics.get_parallel_beam_numeric_slot_time_ms();
            parallel_derived_slot_time_ms = brfs_statistics.get_parallel_beam_derived_slot_time_ms();
            parallel_state_lookup_time_ms = brfs_statistics.get_parallel_beam_state_lookup_time_ms();
            parallel_reached_atom_update_time_ms = brfs_statistics.get_parallel_beam_reached_atom_update_time_ms();
            parallel_ready_queue_high_water = brfs_statistics.get_parallel_beam_ready_queue_high_water();
            parallel_in_flight_chunks_high_water = brfs_statistics.get_parallel_beam_in_flight_chunks_high_water();
            parallel_consumer_stall_time_ms = brfs_statistics.get_parallel_beam_consumer_stall_time_ms();
            parallel_producer_stall_time_ms = brfs_statistics.get_parallel_beam_producer_stall_time_ms();

            const auto& incremental_statistics = brfs_statistics.get_iw1_incremental_first_applicability_statistics();
            incremental_root_actions_fully_enumerated = incremental_statistics.get_num_root_actions_fully_enumerated();
            incremental_non_root_states_using_incremental_path = incremental_statistics.get_num_non_root_states_using_incremental_path();
            incremental_changed_atoms_processed = incremental_statistics.get_num_changed_atoms_processed();
            incremental_trigger_records_visited = incremental_statistics.get_num_trigger_records_visited();
            incremental_partial_seeds_created = incremental_statistics.get_num_partial_seeds_created();
            incremental_ground_actions_returned_by_partial_completion =
                incremental_statistics.get_num_ground_actions_returned_by_partial_completion();
            incremental_local_duplicate_candidates_removed = incremental_statistics.get_num_local_duplicate_candidates_removed();
            incremental_already_tested_actions_skipped = incremental_statistics.get_num_already_tested_actions_skipped();
            incremental_non_root_states_with_zero_returned_actions =
                incremental_statistics.get_num_non_root_states_with_zero_returned_actions();
            incremental_trigger_lookup_time_ms = incremental_statistics.get_trigger_lookup_time_ms();
            incremental_partial_completion_time_ms = incremental_statistics.get_partial_completion_time_ms();
            incremental_debug_crosscheck_time_ms = incremental_statistics.get_debug_crosscheck_time_ms();
        }
        else
        {
            auto options = iw::Options();
            options.max_arity = max_arity;
            options.beam_width = beam_width;
            options.beam_novelty_mode = beam_novelty_mode;
            options.relaxed_survivors_only_beam = relaxed_survivors_only_beam;
            options.parallel_beam_num_threads = num_threads;
            options.parallel_beam_chunk_size = chunk_size;
            options.iw1_precheck_add_effect_novelty = iw1_precheck_add_effect_novelty;
            options.iw1_atom_first_mode = iw1_atom_first_mode;
            options.iw1_atom_first_ratio = iw1_atom_first_ratio;
            options.iw1_incremental_first_applicability = iw1_incremental_first_applicability;
            options.iw1_incremental_first_applicability_debug_crosscheck = iw1_incremental_first_applicability_debug_crosscheck;
            if (!plain_brfs)
            {
                options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
            }
            options.brfs_event_handler = brfs_event_handler;
            options.iw_event_handler = iw_event_handler;

            const auto result = iw::find_solution(search_context, options);
            status = result.status;

            const auto& iw_statistics = iw_event_handler->get_statistics();
            for (const auto& per_arity_brfs_statistics : iw_statistics.get_brfs_statistics_by_arity())
            {
                generated += per_arity_brfs_statistics.get_num_generated();
                novel_generated += per_arity_brfs_statistics.get_num_generated_in_search_tree();
                parallel_chunk_flushes += per_arity_brfs_statistics.get_num_parallel_beam_chunk_flushes();
                parallel_chunk_tasks_total += per_arity_brfs_statistics.get_num_parallel_beam_chunk_tasks_total();
                max_parallel_chunk_size = std::max(max_parallel_chunk_size, per_arity_brfs_statistics.get_max_parallel_beam_chunk_size());
                parallel_worker_compute_time_ms += per_arity_brfs_statistics.get_parallel_beam_worker_compute_time_ms();
                parallel_main_thread_merge_time_ms += per_arity_brfs_statistics.get_parallel_beam_main_thread_merge_time_ms();
                parallel_main_thread_intern_time_ms += per_arity_brfs_statistics.get_parallel_beam_main_thread_intern_time_ms();
                parallel_fluent_slot_time_ms += per_arity_brfs_statistics.get_parallel_beam_fluent_slot_time_ms();
                parallel_numeric_slot_time_ms += per_arity_brfs_statistics.get_parallel_beam_numeric_slot_time_ms();
                parallel_derived_slot_time_ms += per_arity_brfs_statistics.get_parallel_beam_derived_slot_time_ms();
                parallel_state_lookup_time_ms += per_arity_brfs_statistics.get_parallel_beam_state_lookup_time_ms();
                parallel_reached_atom_update_time_ms += per_arity_brfs_statistics.get_parallel_beam_reached_atom_update_time_ms();
                parallel_ready_queue_high_water =
                    std::max(parallel_ready_queue_high_water, per_arity_brfs_statistics.get_parallel_beam_ready_queue_high_water());
                parallel_in_flight_chunks_high_water =
                    std::max(parallel_in_flight_chunks_high_water, per_arity_brfs_statistics.get_parallel_beam_in_flight_chunks_high_water());
                parallel_consumer_stall_time_ms += per_arity_brfs_statistics.get_parallel_beam_consumer_stall_time_ms();
                parallel_producer_stall_time_ms += per_arity_brfs_statistics.get_parallel_beam_producer_stall_time_ms();

                const auto& incremental_statistics = per_arity_brfs_statistics.get_iw1_incremental_first_applicability_statistics();
                incremental_root_actions_fully_enumerated += incremental_statistics.get_num_root_actions_fully_enumerated();
                incremental_non_root_states_using_incremental_path += incremental_statistics.get_num_non_root_states_using_incremental_path();
                incremental_changed_atoms_processed += incremental_statistics.get_num_changed_atoms_processed();
                incremental_trigger_records_visited += incremental_statistics.get_num_trigger_records_visited();
                incremental_partial_seeds_created += incremental_statistics.get_num_partial_seeds_created();
                incremental_ground_actions_returned_by_partial_completion +=
                    incremental_statistics.get_num_ground_actions_returned_by_partial_completion();
                incremental_local_duplicate_candidates_removed += incremental_statistics.get_num_local_duplicate_candidates_removed();
                incremental_already_tested_actions_skipped += incremental_statistics.get_num_already_tested_actions_skipped();
                incremental_non_root_states_with_zero_returned_actions +=
                    incremental_statistics.get_num_non_root_states_with_zero_returned_actions();
                incremental_trigger_lookup_time_ms += incremental_statistics.get_trigger_lookup_time_ms();
                incremental_partial_completion_time_ms += incremental_statistics.get_partial_completion_time_ms();
                incremental_debug_crosscheck_time_ms += incremental_statistics.get_debug_crosscheck_time_ms();
            }
        }
    }
    else
    {
        if (max_arity != 1 && (iw1_novelty_basis == IW1NoveltyBasis::PROJECTIVE || iw1_novelty_basis == IW1NoveltyBasis::PROJECTIVE_TYPED))
        {
            throw std::invalid_argument("Projective IW1 benchmark requires max_arity=1.");
        }
        if (max_arity < 1 || max_arity > 3)
        {
            throw std::invalid_argument("Abstracted IW benchmark requires max_arity in {1, 2, 3}.");
        }

        auto options = brfs::Options();
        options.event_handler = brfs_event_handler;
        if (iw1_novelty_basis == IW1NoveltyBasis::PROJECTIVE || iw1_novelty_basis == IW1NoveltyBasis::PROJECTIVE_TYPED)
        {
            const auto typed_projection = (iw1_novelty_basis == IW1NoveltyBasis::PROJECTIVE_TYPED);
            options.pruning_strategy =
                iw::AbstractedNoveltyPruningStrategyImpl::create(problem, 1, !typed_projection, false, projective_keep_depth_one_novel);
        }
        else
        {
            const auto base_abstracted = (iw1_novelty_basis == IW1NoveltyBasis::ABSTRACTED_BASE);
            options.pruning_strategy = iw::AbstractedNoveltyPruningStrategyImpl::create(
                problem,
                max_arity,
                base_abstracted,
                false,
                projective_keep_depth_one_novel);
        }
        options.max_next_layer_states = std::numeric_limits<uint32_t>::max();
        options.beam_width = static_cast<uint32_t>(beam_width);
        options.beam_novelty_mode = beam_novelty_mode;
        options.relaxed_survivors_only_beam = relaxed_survivors_only_beam;
        options.parallel_beam_num_threads = num_threads;
        options.parallel_beam_chunk_size = chunk_size;
        options.iw1_precheck_add_effect_novelty = iw1_precheck_add_effect_novelty;
        options.iw1_atom_first_mode = iw1_atom_first_mode;
        options.iw1_atom_first_ratio = iw1_atom_first_ratio;
        options.iw1_incremental_first_applicability = iw1_incremental_first_applicability;
        options.iw1_incremental_first_applicability_debug_crosscheck = iw1_incremental_first_applicability_debug_crosscheck;
        if (!plain_brfs)
        {
            options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
        }

        const auto result = brfs::find_solution(search_context, options);
        status = result.status;

        const auto& brfs_statistics = brfs_event_handler->get_statistics();
        generated = brfs_statistics.get_num_generated();
        novel_generated = brfs_statistics.get_num_generated_in_search_tree();
        parallel_chunk_flushes = brfs_statistics.get_num_parallel_beam_chunk_flushes();
        parallel_chunk_tasks_total = brfs_statistics.get_num_parallel_beam_chunk_tasks_total();
        max_parallel_chunk_size = brfs_statistics.get_max_parallel_beam_chunk_size();
        parallel_worker_compute_time_ms = brfs_statistics.get_parallel_beam_worker_compute_time_ms();
        parallel_main_thread_merge_time_ms = brfs_statistics.get_parallel_beam_main_thread_merge_time_ms();
        parallel_main_thread_intern_time_ms = brfs_statistics.get_parallel_beam_main_thread_intern_time_ms();
        parallel_fluent_slot_time_ms = brfs_statistics.get_parallel_beam_fluent_slot_time_ms();
        parallel_numeric_slot_time_ms = brfs_statistics.get_parallel_beam_numeric_slot_time_ms();
        parallel_derived_slot_time_ms = brfs_statistics.get_parallel_beam_derived_slot_time_ms();
        parallel_state_lookup_time_ms = brfs_statistics.get_parallel_beam_state_lookup_time_ms();
        parallel_reached_atom_update_time_ms = brfs_statistics.get_parallel_beam_reached_atom_update_time_ms();
        parallel_ready_queue_high_water = brfs_statistics.get_parallel_beam_ready_queue_high_water();
        parallel_in_flight_chunks_high_water = brfs_statistics.get_parallel_beam_in_flight_chunks_high_water();
        parallel_consumer_stall_time_ms = brfs_statistics.get_parallel_beam_consumer_stall_time_ms();
        parallel_producer_stall_time_ms = brfs_statistics.get_parallel_beam_producer_stall_time_ms();

        const auto& incremental_statistics = brfs_statistics.get_iw1_incremental_first_applicability_statistics();
        incremental_root_actions_fully_enumerated = incremental_statistics.get_num_root_actions_fully_enumerated();
        incremental_non_root_states_using_incremental_path = incremental_statistics.get_num_non_root_states_using_incremental_path();
        incremental_changed_atoms_processed = incremental_statistics.get_num_changed_atoms_processed();
        incremental_trigger_records_visited = incremental_statistics.get_num_trigger_records_visited();
        incremental_partial_seeds_created = incremental_statistics.get_num_partial_seeds_created();
        incremental_ground_actions_returned_by_partial_completion =
            incremental_statistics.get_num_ground_actions_returned_by_partial_completion();
        incremental_local_duplicate_candidates_removed = incremental_statistics.get_num_local_duplicate_candidates_removed();
        incremental_already_tested_actions_skipped = incremental_statistics.get_num_already_tested_actions_skipped();
        incremental_non_root_states_with_zero_returned_actions = incremental_statistics.get_num_non_root_states_with_zero_returned_actions();
        incremental_trigger_lookup_time_ms = incremental_statistics.get_trigger_lookup_time_ms();
        incremental_partial_completion_time_ms = incremental_statistics.get_partial_completion_time_ms();
        incremental_debug_crosscheck_time_ms = incremental_statistics.get_debug_crosscheck_time_ms();
    }

    const auto wall_end = std::chrono::steady_clock::now();

    double lifted_action_generation_time_ms = 0.0;
    double lifted_dynamic_assignment_initialization_time_ms = 0.0;
    double lifted_symmetry_setup_time_ms = 0.0;

    if (const auto lifted_generator = std::dynamic_pointer_cast<KPKCLiftedApplicableActionGeneratorImpl>(search_context->get_applicable_action_generator()))
    {
        const auto& generation_statistics = lifted_generator->get_generation_statistics();
        lifted_action_generation_time_ms = generation_statistics.get_total_generation_time_ms();
        lifted_dynamic_assignment_initialization_time_ms =
            generation_statistics.get_total_dynamic_assignment_initialization_time_ms();
        lifted_symmetry_setup_time_ms = generation_statistics.get_total_symmetry_setup_time_ms();
    }

    return BenchmarkResult {
        status,
        generated,
        novel_generated,
        std::chrono::duration<double, std::milli>(wall_end - wall_start).count(),
        lifted_action_generation_time_ms,
        lifted_dynamic_assignment_initialization_time_ms,
        lifted_symmetry_setup_time_ms,
        parallel_chunk_flushes,
        parallel_chunk_tasks_total,
        max_parallel_chunk_size,
        (parallel_chunk_flushes == 0) ? 0.0 : static_cast<double>(parallel_chunk_tasks_total) / static_cast<double>(parallel_chunk_flushes),
        parallel_worker_compute_time_ms,
        parallel_main_thread_merge_time_ms,
        parallel_main_thread_intern_time_ms,
        parallel_fluent_slot_time_ms,
        parallel_numeric_slot_time_ms,
        parallel_derived_slot_time_ms,
        parallel_state_lookup_time_ms,
        parallel_reached_atom_update_time_ms,
        parallel_ready_queue_high_water,
        parallel_in_flight_chunks_high_water,
        parallel_consumer_stall_time_ms,
        parallel_producer_stall_time_ms,
        incremental_root_actions_fully_enumerated,
        incremental_non_root_states_using_incremental_path,
        incremental_changed_atoms_processed,
        incremental_trigger_records_visited,
        incremental_partial_seeds_created,
        incremental_ground_actions_returned_by_partial_completion,
        incremental_local_duplicate_candidates_removed,
        incremental_already_tested_actions_skipped,
        incremental_non_root_states_with_zero_returned_actions,
        incremental_trigger_lookup_time_ms,
        incremental_partial_completion_time_ms,
        incremental_debug_crosscheck_time_ms,
    };
}

BeamNoveltyMode parse_beam_novelty_mode(const std::string& mode)
{
    if (mode == "all_tested")
    {
        return BeamNoveltyMode::ALL_TESTED;
    }
    if (mode == "survivors_only")
    {
        return BeamNoveltyMode::SURVIVORS_ONLY;
    }

    throw std::invalid_argument("Expected beam novelty mode to be 'all_tested' or 'survivors_only'.");
}

bool is_beam_novelty_mode_token(const std::string& token)
{
    return token == "all_tested" || token == "survivors_only";
}

bool parse_bool_token(const std::string& token)
{
    if (token == "true" || token == "1" || token == "on" || token == "yes")
    {
        return true;
    }
    if (token == "false" || token == "0" || token == "off" || token == "no")
    {
        return false;
    }
    throw std::invalid_argument("Expected boolean token: true|false|1|0|on|off|yes|no.");
}

const char* to_string(SearchStatus status)
{
    switch (status)
    {
        case SearchStatus::IN_PROGRESS:
            return "in_progress";
        case SearchStatus::SOLVED:
            return "solved";
        case SearchStatus::UNSOLVABLE:
            return "unsolvable";
        case SearchStatus::FAILED:
            return "failed";
        case SearchStatus::OUT_OF_STATES:
            return "out_of_states";
        case SearchStatus::OUT_OF_TIME:
            return "out_of_time";
        case SearchStatus::OUT_OF_MEMORY:
            return "out_of_memory";
        case SearchStatus::EXHAUSTED:
            return "exhausted";
    }

    return "unknown";
}
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 7)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <domain.pddl> <problem.pddl> <max_arity> <reps> <all_tested|survivors_only> <threads...> [--beam-width <n>] [--mode <grounded|lifted|lifted_symmetry_pruning|lifted_exhaustive>] [--chunk-sizes <sizes...>] [--relaxed-survivors-only-beam] [--plain-brfs] [--iw1-action-selection <off|action_first|atom_first|both|all>] [--iw1-basis <classical|projective|projective_typed|abstracted_base|abstracted_typed|abstracted|both>] [--iw1-atom-first-ratio <positive_float>] [--iw1-incremental-first-applicability] [--iw1-incremental-first-applicability-debug-crosscheck] [--projective-keep-depth-one-novel <true|false>]\n"
                  << "Legacy usage is still accepted: <...> <max_arity> <beam_width> <reps> <all_tested|survivors_only> <threads...>\n";
        return 1;
    }

    const auto domain_file = std::filesystem::path(argv[1]);
    const auto problem_file = std::filesystem::path(argv[2]);
    const auto max_arity = static_cast<size_t>(std::stoul(argv[3]));

    auto beam_width = static_cast<size_t>(std::numeric_limits<uint32_t>::max());
    auto reps = size_t(0);
    auto beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
    auto positional_start_index = 0;

    // New format (beam optional): <...> <max_arity> <reps> <mode> <threads...>
    // Legacy format:             <...> <max_arity> <beam_width> <reps> <mode> <threads...>
    if ((argc >= 8) && is_beam_novelty_mode_token(argv[6]))
    {
        beam_width = static_cast<size_t>(std::stoul(argv[4]));
        reps = static_cast<size_t>(std::stoul(argv[5]));
        beam_novelty_mode = parse_beam_novelty_mode(argv[6]);
        positional_start_index = 7;
    }
    else if ((argc >= 7) && is_beam_novelty_mode_token(argv[5]))
    {
        reps = static_cast<size_t>(std::stoul(argv[4]));
        beam_novelty_mode = parse_beam_novelty_mode(argv[5]);
        positional_start_index = 6;
    }
    else
    {
        throw std::invalid_argument("Could not parse positional arguments. Expected mode token 'all_tested' or 'survivors_only'.");
    }

    std::vector<uint32_t> thread_counts;
    std::vector<uint32_t> chunk_sizes;
    auto relaxed_survivors_only_beam = false;
    auto plain_brfs = false;
    auto iw1_action_selection_mode = IW1ActionSelectionMode::OFF;
    auto iw1_novelty_basis = IW1NoveltyBasis::CLASSICAL;
    auto iw1_atom_first_ratio = 1.0;
    auto iw1_incremental_first_applicability = false;
    auto iw1_incremental_first_applicability_debug_crosscheck = false;
    auto projective_keep_depth_one_novel = false;
    auto search_context_options = SearchContextImpl::Options(SearchContextImpl::GroundedOptions());
    auto parsing_chunk_sizes = false;
    for (int i = positional_start_index; i < argc; ++i)
    {
        const auto argument = std::string(argv[i]);
        if (argument == "--beam-width")
        {
            if ((i + 1) >= argc)
            {
                throw std::invalid_argument("Expected a beam width after --beam-width.");
            }
            beam_width = static_cast<size_t>(std::stoul(argv[++i]));
            continue;
        }
        if (argument == "--mode")
        {
            if ((i + 1) >= argc)
            {
                throw std::invalid_argument("Expected a mode after --mode.");
            }
            search_context_options = parse_search_mode(argv[++i]);
            continue;
        }
        if (argument == "--chunk-sizes")
        {
            parsing_chunk_sizes = true;
            continue;
        }
        if (argument == "--relaxed-survivors-only-beam")
        {
            relaxed_survivors_only_beam = true;
            continue;
        }
        if (argument == "--plain-brfs")
        {
            plain_brfs = true;
            continue;
        }
        if (argument == "--iw1-action-selection")
        {
            if ((i + 1) >= argc)
            {
                throw std::invalid_argument("Expected IW1 action selection mode after --iw1-action-selection.");
            }
            iw1_action_selection_mode = parse_iw1_action_selection_mode(argv[++i]);
            continue;
        }
        if (argument == "--iw1-basis")
        {
            if ((i + 1) >= argc)
            {
                throw std::invalid_argument("Expected IW1 novelty basis after --iw1-basis.");
            }
            iw1_novelty_basis = parse_iw1_novelty_basis(argv[++i]);
            continue;
        }
        if (argument == "--iw1-atom-first-ratio")
        {
            if ((i + 1) >= argc)
            {
                throw std::invalid_argument("Expected a positive ratio after --iw1-atom-first-ratio.");
            }
            iw1_atom_first_ratio = std::stod(argv[++i]);
            continue;
        }
        if (argument == "--iw1-incremental-first-applicability")
        {
            iw1_incremental_first_applicability = true;
            continue;
        }
        if (argument == "--iw1-incremental-first-applicability-debug-crosscheck")
        {
            iw1_incremental_first_applicability_debug_crosscheck = true;
            continue;
        }
        if (argument == "--projective-keep-depth-one-novel")
        {
            if ((i + 1) >= argc)
            {
                throw std::invalid_argument("Expected boolean value after --projective-keep-depth-one-novel.");
            }
            projective_keep_depth_one_novel = parse_bool_token(argv[++i]);
            continue;
        }
        if (parsing_chunk_sizes)
        {
            chunk_sizes.push_back(static_cast<uint32_t>(std::stoul(argument)));
        }
        else
        {
            thread_counts.push_back(static_cast<uint32_t>(std::stoul(argument)));
        }
    }

    if (beam_width == 0)
    {
        beam_width = static_cast<size_t>(std::numeric_limits<uint32_t>::max());
    }
    if (beam_width > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
    {
        throw std::invalid_argument("Beam width must fit into uint32.");
    }

    if (thread_counts.empty())
    {
        throw std::invalid_argument("Expected at least one thread count.");
    }

    if (chunk_sizes.empty())
    {
        chunk_sizes.push_back(1024);
    }

    if (relaxed_survivors_only_beam && beam_novelty_mode != BeamNoveltyMode::SURVIVORS_ONLY)
    {
        throw std::invalid_argument("--relaxed-survivors-only-beam requires beam novelty mode 'survivors_only'.");
    }
    if (iw1_atom_first_ratio <= 0.0)
    {
        throw std::invalid_argument("--iw1-atom-first-ratio must be positive.");
    }
    if (iw1_incremental_first_applicability_debug_crosscheck && !iw1_incremental_first_applicability)
    {
        throw std::invalid_argument(
            "--iw1-incremental-first-applicability-debug-crosscheck requires --iw1-incremental-first-applicability.");
    }

    struct IW1Variant
    {
        const char* label;
        bool iw1_precheck_add_effect_novelty;
        bool iw1_atom_first_mode;
    };

    auto iw1_variants = std::vector<IW1Variant> {};
    switch (iw1_action_selection_mode)
    {
        case IW1ActionSelectionMode::OFF:
            iw1_variants.push_back({ "off", false, false });
            break;
        case IW1ActionSelectionMode::ACTION_FIRST:
            iw1_variants.push_back({ "action_first", true, false });
            break;
        case IW1ActionSelectionMode::ATOM_FIRST:
            iw1_variants.push_back({ "atom_first", true, true });
            break;
        case IW1ActionSelectionMode::BOTH:
            iw1_variants.push_back({ "action_first", true, false });
            iw1_variants.push_back({ "atom_first", true, true });
            break;
        case IW1ActionSelectionMode::ALL:
            iw1_variants.push_back({ "off", false, false });
            iw1_variants.push_back({ "action_first", true, false });
            iw1_variants.push_back({ "atom_first", true, true });
            break;
    }

    auto iw1_bases = std::vector<IW1NoveltyBasis> {};
    switch (iw1_novelty_basis)
    {
        case IW1NoveltyBasis::CLASSICAL:
            iw1_bases.push_back(IW1NoveltyBasis::CLASSICAL);
            break;
        case IW1NoveltyBasis::PROJECTIVE:
            iw1_bases.push_back(IW1NoveltyBasis::PROJECTIVE);
            break;
        case IW1NoveltyBasis::PROJECTIVE_TYPED:
            iw1_bases.push_back(IW1NoveltyBasis::PROJECTIVE_TYPED);
            break;
        case IW1NoveltyBasis::ABSTRACTED_BASE:
            iw1_bases.push_back(IW1NoveltyBasis::ABSTRACTED_BASE);
            break;
        case IW1NoveltyBasis::ABSTRACTED_TYPED:
            iw1_bases.push_back(IW1NoveltyBasis::ABSTRACTED_TYPED);
            break;
        case IW1NoveltyBasis::BOTH:
            iw1_bases.push_back(IW1NoveltyBasis::CLASSICAL);
            iw1_bases.push_back(IW1NoveltyBasis::PROJECTIVE);
            break;
    }

    const auto mode_name = [&]() -> std::string
    {
        return std::visit(
            [](auto&& mode) -> std::string
            {
                using ModeT = std::decay_t<decltype(mode)>;
                if constexpr (std::is_same_v<ModeT, SearchContextImpl::GroundedOptions>)
                {
                    return "grounded";
                }
                else if constexpr (std::is_same_v<ModeT, SearchContextImpl::LiftedOptions>)
                {
                    return std::visit(
                        [](auto&& option) -> std::string
                        {
                            using OptionT = std::decay_t<decltype(option)>;
                            if constexpr (std::is_same_v<OptionT, SearchContextImpl::LiftedOptions::KPKCOptions>)
                            {
                                return (option.pruning == SearchContextImpl::SymmetryPruning::GI) ? "lifted_symmetry_pruning" : "lifted";
                            }
                            else
                            {
                                return "lifted_exhaustive";
                            }
                        },
                        mode.option);
                }
                else
                {
                    throw std::logic_error("Missing benchmark search mode.");
                }
            },
            search_context_options.mode);
    }();

    const auto beam_is_off = (beam_width == static_cast<size_t>(std::numeric_limits<uint32_t>::max()));
    std::cout << "=== Benchmark Configuration ===\n";
    std::cout << "search_mode: " << mode_name << '\n';
    std::cout << "domain:      " << domain_file << '\n';
    std::cout << "problem:     " << problem_file << '\n';
    std::cout << "max_arity:   " << max_arity << '\n';
    std::cout << "beam:        " << (beam_is_off ? "off" : std::to_string(beam_width)) << '\n';
    std::cout << "beam_mode:   " << (beam_novelty_mode == BeamNoveltyMode::ALL_TESTED ? "all_tested" : "survivors_only") << '\n';
    std::cout << "reps:        " << reps << '\n';
    std::cout << "relaxed_survivors_only_beam: " << (relaxed_survivors_only_beam ? "true" : "false") << '\n';
    std::cout << "plain_brfs: " << (plain_brfs ? "true" : "false") << '\n';
    std::cout << "iw1_basis:   " << to_string(iw1_novelty_basis) << '\n';
    std::cout << "iw1_action_selection: " << to_string(iw1_action_selection_mode) << '\n';
    std::cout << "iw1_atom_first_ratio: " << iw1_atom_first_ratio << "\n";
    std::cout << "iw1_incremental_first_applicability: " << (iw1_incremental_first_applicability ? "true" : "false") << "\n";
    std::cout << "iw1_incremental_first_applicability_debug_crosscheck: "
              << (iw1_incremental_first_applicability_debug_crosscheck ? "true" : "false") << "\n";
    std::cout << "projective_keep_depth_one_novel: " << (projective_keep_depth_one_novel ? "true" : "false") << "\n";

    for (const auto basis : iw1_bases)
    {
        auto action_first_medians_ms = std::map<std::pair<uint32_t, uint32_t>, double> {};
        auto action_first_generated = std::map<std::pair<uint32_t, uint32_t>, size_t> {};
        auto action_first_novel_generated = std::map<std::pair<uint32_t, uint32_t>, size_t> {};
        auto action_first_status = std::map<std::pair<uint32_t, uint32_t>, SearchStatus> {};
        std::cout << "\n=== IW1 Basis: " << to_string(basis) << " ===\n";

        for (const auto& iw1_variant : iw1_variants)
        {
            std::cout << "\n  Variant: " << iw1_variant.label << '\n';
            std::cout << "    iw1_precheck_add_effect_novelty: " << (iw1_variant.iw1_precheck_add_effect_novelty ? "true" : "false") << '\n';
            std::cout << "    iw1_atom_first_mode: " << (iw1_variant.iw1_atom_first_mode ? "true" : "false") << '\n';

            for (const auto chunk_size : chunk_sizes)
            {
                std::cout << "    chunk_size: " << chunk_size << '\n';
                std::optional<double> serial_median_ms;
                const auto need_explicit_serial_reference =
                    relaxed_survivors_only_beam || std::none_of(thread_counts.begin(), thread_counts.end(), [](const auto count) { return count <= 1; });
                if (need_explicit_serial_reference)
                {
                    auto serial_wall_times_ms = std::vector<double> {};
                    serial_wall_times_ms.reserve(reps);
                    for (size_t rep = 0; rep < reps; ++rep)
                    {
                        const auto serial_result = run_once(domain_file,
                                                            problem_file,
                                                            search_context_options,
                                                            max_arity,
                                                            beam_width,
                                                            beam_novelty_mode,
                                                            false,
                                                            1,
                                                            chunk_size,
                                                            plain_brfs,
                                                            iw1_variant.iw1_precheck_add_effect_novelty,
                                                            iw1_variant.iw1_atom_first_mode,
                                                            iw1_atom_first_ratio,
                                                            iw1_incremental_first_applicability,
                                                            iw1_incremental_first_applicability_debug_crosscheck,
                                                            basis,
                                                            projective_keep_depth_one_novel);
                        serial_wall_times_ms.push_back(serial_result.wall_time_ms);
                    }

                    std::sort(serial_wall_times_ms.begin(), serial_wall_times_ms.end());
                    serial_median_ms = serial_wall_times_ms[serial_wall_times_ms.size() / 2];
                    std::cout << "      serial_reference_ms: " << serial_median_ms.value() << '\n';
                }

                for (const auto num_threads : thread_counts)
                {
                    if (relaxed_survivors_only_beam && num_threads <= 1)
                    {
                        std::cout << "      threads: " << num_threads << " (skipped: relaxed_survivors_only_beam requires parallel threads)";
                        if (serial_median_ms.has_value())
                        {
                            std::cout << " serial_reference_ms=" << serial_median_ms.value();
                        }
                        std::cout << '\n';
                        continue;
                    }

                    auto wall_times_ms = std::vector<double> {};
                    wall_times_ms.reserve(reps);

                auto status = SearchStatus::FAILED;
                size_t generated = 0;
                size_t novel_generated = 0;
                uint64_t parallel_chunk_flushes = 0;
                uint64_t parallel_chunk_tasks_total = 0;
                uint64_t max_parallel_chunk_size = 0;
                double lifted_action_generation_time_ms = 0.0;
                double lifted_dynamic_assignment_initialization_time_ms = 0.0;
                double lifted_symmetry_setup_time_ms = 0.0;
                double average_parallel_chunk_size = 0.0;
                double parallel_worker_compute_time_ms = 0.0;
                double parallel_main_thread_merge_time_ms = 0.0;
                double parallel_main_thread_intern_time_ms = 0.0;
                double parallel_fluent_slot_time_ms = 0.0;
                double parallel_numeric_slot_time_ms = 0.0;
                double parallel_derived_slot_time_ms = 0.0;
                double parallel_state_lookup_time_ms = 0.0;
                double parallel_reached_atom_update_time_ms = 0.0;
                uint64_t parallel_ready_queue_high_water = 0;
                uint64_t parallel_in_flight_chunks_high_water = 0;
                double parallel_consumer_stall_time_ms = 0.0;
                double parallel_producer_stall_time_ms = 0.0;
                uint64_t incremental_root_actions_fully_enumerated = 0;
                uint64_t incremental_non_root_states_using_incremental_path = 0;
                uint64_t incremental_changed_atoms_processed = 0;
                uint64_t incremental_trigger_records_visited = 0;
                uint64_t incremental_partial_seeds_created = 0;
                uint64_t incremental_ground_actions_returned_by_partial_completion = 0;
                uint64_t incremental_local_duplicate_candidates_removed = 0;
                uint64_t incremental_already_tested_actions_skipped = 0;
                uint64_t incremental_non_root_states_with_zero_returned_actions = 0;
                double incremental_trigger_lookup_time_ms = 0.0;
                double incremental_partial_completion_time_ms = 0.0;
                double incremental_debug_crosscheck_time_ms = 0.0;

                for (size_t rep = 0; rep < reps; ++rep)
                {
                    const auto result = run_once(domain_file,
                                                 problem_file,
                                                 search_context_options,
                                                 max_arity,
                                                 beam_width,
                                                 beam_novelty_mode,
                                                 relaxed_survivors_only_beam,
                                                 num_threads,
                                                 chunk_size,
                                                 plain_brfs,
                                                 iw1_variant.iw1_precheck_add_effect_novelty,
                                                 iw1_variant.iw1_atom_first_mode,
                                                 iw1_atom_first_ratio,
                                                 iw1_incremental_first_applicability,
                                                 iw1_incremental_first_applicability_debug_crosscheck,
                                                 basis,
                                                 projective_keep_depth_one_novel);
                    status = result.status;
                    generated = result.generated;
                    novel_generated = result.novel_generated;
                    parallel_chunk_flushes = result.parallel_chunk_flushes;
                    parallel_chunk_tasks_total = result.parallel_chunk_tasks_total;
                    max_parallel_chunk_size = result.max_parallel_chunk_size;
                    lifted_action_generation_time_ms = result.lifted_action_generation_time_ms;
                    lifted_dynamic_assignment_initialization_time_ms = result.lifted_dynamic_assignment_initialization_time_ms;
                    lifted_symmetry_setup_time_ms = result.lifted_symmetry_setup_time_ms;
                    average_parallel_chunk_size = result.average_parallel_chunk_size;
                    parallel_worker_compute_time_ms = result.parallel_worker_compute_time_ms;
                    parallel_main_thread_merge_time_ms = result.parallel_main_thread_merge_time_ms;
                    parallel_main_thread_intern_time_ms = result.parallel_main_thread_intern_time_ms;
                    parallel_fluent_slot_time_ms = result.parallel_fluent_slot_time_ms;
                    parallel_numeric_slot_time_ms = result.parallel_numeric_slot_time_ms;
                    parallel_derived_slot_time_ms = result.parallel_derived_slot_time_ms;
                    parallel_state_lookup_time_ms = result.parallel_state_lookup_time_ms;
                    parallel_reached_atom_update_time_ms = result.parallel_reached_atom_update_time_ms;
                    parallel_ready_queue_high_water = result.parallel_ready_queue_high_water;
                    parallel_in_flight_chunks_high_water = result.parallel_in_flight_chunks_high_water;
                    parallel_consumer_stall_time_ms = result.parallel_consumer_stall_time_ms;
                    parallel_producer_stall_time_ms = result.parallel_producer_stall_time_ms;
                    incremental_root_actions_fully_enumerated = result.incremental_root_actions_fully_enumerated;
                    incremental_non_root_states_using_incremental_path = result.incremental_non_root_states_using_incremental_path;
                    incremental_changed_atoms_processed = result.incremental_changed_atoms_processed;
                    incremental_trigger_records_visited = result.incremental_trigger_records_visited;
                    incremental_partial_seeds_created = result.incremental_partial_seeds_created;
                    incremental_ground_actions_returned_by_partial_completion =
                        result.incremental_ground_actions_returned_by_partial_completion;
                    incremental_local_duplicate_candidates_removed = result.incremental_local_duplicate_candidates_removed;
                    incremental_already_tested_actions_skipped = result.incremental_already_tested_actions_skipped;
                    incremental_non_root_states_with_zero_returned_actions =
                        result.incremental_non_root_states_with_zero_returned_actions;
                    incremental_trigger_lookup_time_ms = result.incremental_trigger_lookup_time_ms;
                    incremental_partial_completion_time_ms = result.incremental_partial_completion_time_ms;
                    incremental_debug_crosscheck_time_ms = result.incremental_debug_crosscheck_time_ms;
                    wall_times_ms.push_back(result.wall_time_ms);
                }

                std::sort(wall_times_ms.begin(), wall_times_ms.end());
                const auto median_ms = wall_times_ms[wall_times_ms.size() / 2];
                const auto mean_ms = std::accumulate(wall_times_ms.begin(), wall_times_ms.end(), 0.0) / static_cast<double>(wall_times_ms.size());
                const auto min_ms = wall_times_ms.front();
                const auto max_ms = wall_times_ms.back();

                if (num_threads <= 1)
                {
                    serial_median_ms = median_ms;
                }

                std::cout << "      threads: " << num_threads << '\n';
                std::cout << "        status: " << to_string(status) << '\n';
                std::cout << "        generated: " << generated << '\n';
                std::cout << "        novel_generated: " << novel_generated << '\n';
                std::cout << "        wall_ms: median=" << median_ms << " mean=" << mean_ms << " min=" << min_ms << " max=" << max_ms << '\n';
                std::cout << "        chunking: flushes=" << parallel_chunk_flushes
                          << " avg_chunk_size=" << average_parallel_chunk_size
                          << " max_chunk_size=" << max_parallel_chunk_size
                          << " chunk_tasks_total=" << parallel_chunk_tasks_total << '\n';
                std::cout << "        lifted_ms: action_gen=" << lifted_action_generation_time_ms
                          << " dynamic_assign=" << lifted_dynamic_assignment_initialization_time_ms
                          << " symmetry_setup=" << lifted_symmetry_setup_time_ms << '\n';
                std::cout << "        parallel_ms: worker_compute=" << parallel_worker_compute_time_ms
                          << " main_merge=" << parallel_main_thread_merge_time_ms
                          << " main_intern=" << parallel_main_thread_intern_time_ms
                          << " fluent_slot=" << parallel_fluent_slot_time_ms
                          << " numeric_slot=" << parallel_numeric_slot_time_ms
                          << " derived_slot=" << parallel_derived_slot_time_ms
                          << " state_lookup=" << parallel_state_lookup_time_ms
                          << " reached_atoms=" << parallel_reached_atom_update_time_ms
                          << " consumer_stall=" << parallel_consumer_stall_time_ms
                          << " producer_stall=" << parallel_producer_stall_time_ms << '\n';
                std::cout << "        parallel_hwm: ready_queue=" << parallel_ready_queue_high_water
                          << " in_flight=" << parallel_in_flight_chunks_high_water << '\n';
                std::cout << "        incremental: root_actions=" << incremental_root_actions_fully_enumerated
                          << " non_root_states=" << incremental_non_root_states_using_incremental_path
                          << " changed_atoms=" << incremental_changed_atoms_processed
                          << " triggers=" << incremental_trigger_records_visited
                          << " seeds=" << incremental_partial_seeds_created
                          << " completed_actions=" << incremental_ground_actions_returned_by_partial_completion
                          << " local_duplicates_removed=" << incremental_local_duplicate_candidates_removed
                          << " already_tested_skipped=" << incremental_already_tested_actions_skipped
                          << " zero_return_states=" << incremental_non_root_states_with_zero_returned_actions << '\n';
                std::cout << "        incremental_ms: trigger_lookup=" << incremental_trigger_lookup_time_ms
                          << " partial_completion=" << incremental_partial_completion_time_ms
                          << " debug_crosscheck=" << incremental_debug_crosscheck_time_ms << '\n';
                if (serial_median_ms.has_value())
                {
                    std::cout << "        speedup_vs_serial: " << (serial_median_ms.value() / median_ms) << '\n';
                }

                if (iw1_action_selection_mode == IW1ActionSelectionMode::BOTH || iw1_action_selection_mode == IW1ActionSelectionMode::ALL)
                {
                    const auto key = std::make_pair(chunk_size, num_threads);
                    if (std::string(iw1_variant.label) == "action_first")
                    {
                        action_first_medians_ms[key] = median_ms;
                        action_first_generated[key] = generated;
                        action_first_novel_generated[key] = novel_generated;
                        action_first_status[key] = status;
                    }
                    else if (std::string(iw1_variant.label) == "atom_first")
                    {
                        const auto median_it = action_first_medians_ms.find(key);
                        if (median_it != action_first_medians_ms.end())
                        {
                            const auto action_first_median_ms = median_it->second;
                            const auto atom_first_speedup_vs_action_first = action_first_median_ms / median_ms;
                            std::cout << "      comparison (atom_first vs action_first): chunk_size=" << chunk_size
                                      << " threads=" << num_threads
                                      << " action_first_median_ms=" << action_first_median_ms
                                      << " atom_first_median_ms=" << median_ms
                                      << " atom_first_speedup_vs_action_first=" << atom_first_speedup_vs_action_first;
                            const auto generated_it = action_first_generated.find(key);
                            if ((generated_it != action_first_generated.end()) && (generated_it->second != generated))
                            {
                                std::cout << " generated_mismatch=true action_first_generated=" << generated_it->second
                                          << " atom_first_generated=" << generated;
                            }
                            const auto novel_generated_it = action_first_novel_generated.find(key);
                            if ((novel_generated_it != action_first_novel_generated.end()) && (novel_generated_it->second != novel_generated))
                            {
                                std::cout << " novel_generated_mismatch=true action_first_novel_generated=" << novel_generated_it->second
                                          << " atom_first_novel_generated=" << novel_generated;
                            }
                            const auto status_it = action_first_status.find(key);
                            if ((status_it != action_first_status.end()) && (status_it->second != status))
                            {
                                std::cout << " status_mismatch=true action_first_status=" << to_string(status_it->second)
                                          << " atom_first_status=" << to_string(status);
                            }
                            std::cout << '\n';
                        }
                    }
                }
            }
        }
    }

    }

    return 0;
}
