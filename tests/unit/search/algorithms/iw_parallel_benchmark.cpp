#include "mimir/formalism/problem.hpp"
#include "mimir/search/algorithms/brfs/event_handlers.hpp"
#include "mimir/search/algorithms/iw.hpp"
#include "mimir/search/algorithms/iw/event_handlers.hpp"
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
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mimir::formalism;
using namespace mimir::search;

namespace
{
struct BenchmarkResult
{
    SearchStatus status;
    size_t generated;
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

BenchmarkResult run_once(const std::filesystem::path& domain_file,
                         const std::filesystem::path& problem_file,
                         const SearchContextImpl::Options& search_context_options,
                         size_t max_arity,
                         size_t beam_width,
                         BeamNoveltyMode beam_novelty_mode,
                         bool relaxed_survivors_only_beam,
                         uint32_t num_threads,
                         uint32_t chunk_size)
{
    auto search_context = SearchContextImpl::create(domain_file, problem_file, search_context_options);
    const auto problem = search_context->get_problem();
    auto brfs_event_handler = brfs::DefaultEventHandlerImpl::create(problem, true);
    auto iw_event_handler = iw::DefaultEventHandlerImpl::create(problem, true);

    auto options = iw::Options();
    options.max_arity = max_arity;
    options.beam_width = beam_width;
    options.beam_novelty_mode = beam_novelty_mode;
    options.relaxed_survivors_only_beam = relaxed_survivors_only_beam;
    options.parallel_beam_num_threads = num_threads;
    options.parallel_beam_chunk_size = chunk_size;
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
    options.brfs_event_handler = brfs_event_handler;
    options.iw_event_handler = iw_event_handler;

    const auto wall_start = std::chrono::steady_clock::now();
    const auto result = iw::find_solution(search_context, options);
    const auto wall_end = std::chrono::steady_clock::now();

    const auto& iw_statistics = iw_event_handler->get_statistics();
    size_t generated = 0;
    uint64_t parallel_chunk_flushes = 0;
    uint64_t parallel_chunk_tasks_total = 0;
    uint64_t max_parallel_chunk_size = 0;
    double lifted_action_generation_time_ms = 0.0;
    double lifted_dynamic_assignment_initialization_time_ms = 0.0;
    double lifted_symmetry_setup_time_ms = 0.0;
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
    for (const auto& brfs_statistics : iw_statistics.get_brfs_statistics_by_arity())
    {
        generated += brfs_statistics.get_num_generated();
        parallel_chunk_flushes += brfs_statistics.get_num_parallel_beam_chunk_flushes();
        parallel_chunk_tasks_total += brfs_statistics.get_num_parallel_beam_chunk_tasks_total();
        max_parallel_chunk_size = std::max(max_parallel_chunk_size, brfs_statistics.get_max_parallel_beam_chunk_size());
        parallel_worker_compute_time_ms += brfs_statistics.get_parallel_beam_worker_compute_time_ms();
        parallel_main_thread_merge_time_ms += brfs_statistics.get_parallel_beam_main_thread_merge_time_ms();
        parallel_main_thread_intern_time_ms += brfs_statistics.get_parallel_beam_main_thread_intern_time_ms();
        parallel_fluent_slot_time_ms += brfs_statistics.get_parallel_beam_fluent_slot_time_ms();
        parallel_numeric_slot_time_ms += brfs_statistics.get_parallel_beam_numeric_slot_time_ms();
        parallel_derived_slot_time_ms += brfs_statistics.get_parallel_beam_derived_slot_time_ms();
        parallel_state_lookup_time_ms += brfs_statistics.get_parallel_beam_state_lookup_time_ms();
        parallel_reached_atom_update_time_ms += brfs_statistics.get_parallel_beam_reached_atom_update_time_ms();
        parallel_ready_queue_high_water = std::max(parallel_ready_queue_high_water, brfs_statistics.get_parallel_beam_ready_queue_high_water());
        parallel_in_flight_chunks_high_water =
            std::max(parallel_in_flight_chunks_high_water, brfs_statistics.get_parallel_beam_in_flight_chunks_high_water());
        parallel_consumer_stall_time_ms += brfs_statistics.get_parallel_beam_consumer_stall_time_ms();
        parallel_producer_stall_time_ms += brfs_statistics.get_parallel_beam_producer_stall_time_ms();
    }

    if (const auto lifted_generator = std::dynamic_pointer_cast<KPKCLiftedApplicableActionGeneratorImpl>(search_context->get_applicable_action_generator()))
    {
        const auto& generation_statistics = lifted_generator->get_generation_statistics();
        lifted_action_generation_time_ms = generation_statistics.get_total_generation_time_ms();
        lifted_dynamic_assignment_initialization_time_ms =
            generation_statistics.get_total_dynamic_assignment_initialization_time_ms();
        lifted_symmetry_setup_time_ms = generation_statistics.get_total_symmetry_setup_time_ms();
    }

    return BenchmarkResult {
        result.status,
        generated,
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
    if (argc < 8)
    {
        std::cerr << "Usage: " << argv[0]
                  << " <domain.pddl> <problem.pddl> <max_arity> <beam_width> <reps> <all_tested|survivors_only> <threads...> [--mode <grounded|lifted|lifted_symmetry_pruning|lifted_exhaustive>] [--chunk-sizes <sizes...>] [--relaxed-survivors-only-beam]\n";
        return 1;
    }

    const auto domain_file = std::filesystem::path(argv[1]);
    const auto problem_file = std::filesystem::path(argv[2]);
    const auto max_arity = static_cast<size_t>(std::stoul(argv[3]));
    const auto beam_width = static_cast<size_t>(std::stoul(argv[4]));
    const auto reps = static_cast<size_t>(std::stoul(argv[5]));
    const auto beam_novelty_mode = parse_beam_novelty_mode(argv[6]);

    std::vector<uint32_t> thread_counts;
    std::vector<uint32_t> chunk_sizes;
    auto relaxed_survivors_only_beam = false;
    auto search_context_options = SearchContextImpl::Options(SearchContextImpl::GroundedOptions());
    auto parsing_chunk_sizes = false;
    for (int i = 7; i < argc; ++i)
    {
        const auto argument = std::string(argv[i]);
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
        if (parsing_chunk_sizes)
        {
            chunk_sizes.push_back(static_cast<uint32_t>(std::stoul(argument)));
        }
        else
        {
            thread_counts.push_back(static_cast<uint32_t>(std::stoul(argument)));
        }
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

    std::cout << "search_mode=" << mode_name << " domain=" << domain_file << " problem=" << problem_file << " max_arity=" << max_arity
              << " beam_width=" << beam_width << " mode=" << argv[6] << " reps=" << reps
              << " relaxed_survivors_only_beam=" << (relaxed_survivors_only_beam ? "true" : "false") << '\n';

    for (const auto chunk_size : chunk_sizes)
    {
        std::cout << "chunk_size=" << chunk_size << '\n';
        std::optional<double> serial_median_ms;

        for (const auto num_threads : thread_counts)
        {
            auto wall_times_ms = std::vector<double> {};
            wall_times_ms.reserve(reps);

            auto status = SearchStatus::FAILED;
            size_t generated = 0;
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
                                             chunk_size);
                status = result.status;
                generated = result.generated;
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

            std::cout << "threads=" << num_threads << " median_wall_ms=" << median_ms << " mean_wall_ms=" << mean_ms
                      << " min_ms=" << min_ms << " max_ms=" << max_ms << " generated=" << generated << " status=" << to_string(status)
                      << " chunk_flushes=" << parallel_chunk_flushes << " avg_chunk_size=" << average_parallel_chunk_size
                      << " max_chunk_size=" << max_parallel_chunk_size << " chunk_tasks_total=" << parallel_chunk_tasks_total
                      << " lifted_action_gen_ms=" << lifted_action_generation_time_ms
                      << " lifted_dynamic_assign_ms=" << lifted_dynamic_assignment_initialization_time_ms
                      << " lifted_symmetry_setup_ms=" << lifted_symmetry_setup_time_ms
                      << " worker_compute_ms=" << parallel_worker_compute_time_ms
                      << " main_merge_ms=" << parallel_main_thread_merge_time_ms
                      << " main_intern_ms=" << parallel_main_thread_intern_time_ms
                      << " fluent_slot_ms=" << parallel_fluent_slot_time_ms
                      << " numeric_slot_ms=" << parallel_numeric_slot_time_ms
                      << " derived_slot_ms=" << parallel_derived_slot_time_ms
                      << " state_lookup_ms=" << parallel_state_lookup_time_ms
                      << " reached_atoms_ms=" << parallel_reached_atom_update_time_ms
                      << " ready_queue_hwm=" << parallel_ready_queue_high_water
                      << " in_flight_hwm=" << parallel_in_flight_chunks_high_water
                      << " consumer_stall_ms=" << parallel_consumer_stall_time_ms
                      << " producer_stall_ms=" << parallel_producer_stall_time_ms;
            if (serial_median_ms.has_value())
            {
                std::cout << " speedup_vs_1=" << (serial_median_ms.value() / median_ms);
            }
            std::cout << '\n';
        }
    }

    return 0;
}
