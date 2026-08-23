/*
 * Micro-benchmark for the *non-batched* serial IW(1) path.
 *
 * Its only purpose is to prove that adding the batched parallel-rollout entry
 * point leaves the existing serial path's timing unchanged. Run it before and
 * after the change on the same instances and compare.
 *
 * Reports the median of `--repeats` runs, which is far more stable than the mean
 * under macOS scheduler noise.
 */

#include <algorithm>
#include <argparse/argparse.hpp>
#include <chrono>
#include <iostream>
#include <mimir/mimir.hpp>
#include <vector>

using namespace mimir;
using namespace mimir::search;
using namespace mimir::formalism;

int main(int argc, char** argv)
{
    auto program = argparse::ArgumentParser("bench_serial_iw");
    program.add_argument("-D", "--domain-filepath").required();
    program.add_argument("-P", "--problem-filepath").required();
    program.add_argument("-R", "--repeats").default_value(size_t(9)).scan<'u', size_t>();
    program.add_argument("-A", "--arity").default_value(size_t(1)).scan<'u', size_t>();

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err)
    {
        std::cerr << err.what() << "\n" << program;
        return 1;
    }

    const auto domain_filepath = program.get<std::string>("--domain-filepath");
    const auto problem_filepath = program.get<std::string>("--problem-filepath");
    const auto repeats = program.get<size_t>("--repeats");
    const auto arity = program.get<size_t>("--arity");

    auto search_micros = std::vector<int64_t> {};

    for (size_t r = 0; r < repeats; ++r)
    {
        // A fresh Problem each repeat, so every run sees cold interning tables and
        // an identical amount of work. Parsing/grounding is excluded from the timing.
        const auto problem = ProblemImpl::create(domain_filepath, problem_filepath);
        const auto context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));

        auto options = iw::Options();
        options.max_arity = arity;
        options.iw_event_handler = iw::DefaultEventHandlerImpl::create(problem, true);
        options.brfs_event_handler = brfs::DefaultEventHandlerImpl::create(problem, true);

        const auto t0 = std::chrono::steady_clock::now();
        const auto result = iw::find_solution(context, options);
        const auto t1 = std::chrono::steady_clock::now();

        search_micros.push_back(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        std::cout << "  run " << r << ": " << search_micros.back() << " us, states="
                  << context->get_state_repository()->get_state_count() << ", status=" << static_cast<int>(result.status) << "\n";
    }

    std::sort(search_micros.begin(), search_micros.end());
    std::cout << "MEDIAN_US " << search_micros[search_micros.size() / 2] << "  MIN_US " << search_micros.front() << "  MAX_US " << search_micros.back()
              << "\n";

    return 0;
}
