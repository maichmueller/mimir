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
};

BenchmarkResult run_once(const std::filesystem::path& domain_file,
                         const std::filesystem::path& problem_file,
                         size_t max_arity,
                         size_t beam_width,
                         BeamNoveltyMode beam_novelty_mode,
                         uint32_t num_threads)
{
    auto problem = ProblemImpl::create(domain_file, problem_file);
    auto grounder = LiftedGrounder(problem);
    auto applicable_action_generator =
        grounder.create_grounded_applicable_action_generator(match_tree::Options(), GroundedApplicableActionGeneratorImpl::DefaultEventHandlerImpl::create());
    auto axiom_evaluator =
        grounder.create_grounded_axiom_evaluator(match_tree::Options(), GroundedAxiomEvaluatorImpl::DefaultEventHandlerImpl::create());
    auto state_repository = StateRepositoryImpl::create(axiom_evaluator);
    auto brfs_event_handler = brfs::DefaultEventHandlerImpl::create(problem, true);
    auto iw_event_handler = iw::DefaultEventHandlerImpl::create(problem, true);
    auto search_context = SearchContextImpl::create(problem, applicable_action_generator, state_repository);

    auto options = iw::Options();
    options.max_arity = max_arity;
    options.beam_width = beam_width;
    options.beam_novelty_mode = beam_novelty_mode;
    options.parallel_beam_num_threads = num_threads;
    options.layer_ordering_strategy = GoalCountLayerOrderingStrategyImpl::create(problem);
    options.brfs_event_handler = brfs_event_handler;
    options.iw_event_handler = iw_event_handler;

    const auto wall_start = std::chrono::steady_clock::now();
    const auto result = iw::find_solution(search_context, options);
    const auto wall_end = std::chrono::steady_clock::now();

    const auto& iw_statistics = iw_event_handler->get_statistics();
    size_t generated = 0;
    for (const auto& brfs_statistics : iw_statistics.get_brfs_statistics_by_arity())
    {
        generated += brfs_statistics.get_num_generated();
    }

    return BenchmarkResult {
        result.status,
        generated,
        std::chrono::duration<double, std::milli>(wall_end - wall_start).count(),
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
                  << " <domain.pddl> <problem.pddl> <max_arity> <beam_width> <reps> <all_tested|survivors_only> <threads...>\n";
        return 1;
    }

    const auto domain_file = std::filesystem::path(argv[1]);
    const auto problem_file = std::filesystem::path(argv[2]);
    const auto max_arity = static_cast<size_t>(std::stoul(argv[3]));
    const auto beam_width = static_cast<size_t>(std::stoul(argv[4]));
    const auto reps = static_cast<size_t>(std::stoul(argv[5]));
    const auto beam_novelty_mode = parse_beam_novelty_mode(argv[6]);

    std::vector<uint32_t> thread_counts;
    thread_counts.reserve(static_cast<size_t>(argc - 7));
    for (int i = 7; i < argc; ++i)
    {
        thread_counts.push_back(static_cast<uint32_t>(std::stoul(argv[i])));
    }

    std::cout << "domain=" << domain_file << " problem=" << problem_file << " max_arity=" << max_arity << " beam_width=" << beam_width
              << " mode=" << argv[6] << " reps=" << reps << '\n';

    std::optional<double> serial_median_ms;

    for (const auto num_threads : thread_counts)
    {
        auto wall_times_ms = std::vector<double> {};
        wall_times_ms.reserve(reps);

        auto status = SearchStatus::FAILED;
        size_t generated = 0;

        for (size_t rep = 0; rep < reps; ++rep)
        {
            const auto result = run_once(domain_file, problem_file, max_arity, beam_width, beam_novelty_mode, num_threads);
            status = result.status;
            generated = result.generated;
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
                  << " min_ms=" << min_ms << " max_ms=" << max_ms << " generated=" << generated << " status=" << to_string(status);
        if (serial_median_ms.has_value())
        {
            std::cout << " speedup_vs_1=" << (serial_median_ms.value() / median_ms);
        }
        std::cout << '\n';
    }

    return 0;
}
