#include "mimir/mimir.hpp"

#include <benchmark/benchmark.h>

using namespace mimir;

static void BM_iw(benchmark::State& state)
{
    const auto domain_folder = fs::path(DATA_DIR) / "pushworld";
    const auto domain_file = domain_folder / "domain.pddl";
    const auto problem_file = domain_folder / "problem.pddl";
    const auto state_space = StateSpace::create(domain_file, problem_file).value();

    for (auto _ : state)
    {
        IWAlgorithm iw { state_space, 4 };
        Plan out_plan {};
        iw.find_solution(out_plan);
    }
}

// Register the iw benchmark
BENCHMARK(BM_iw)->Unit(benchmark::kMillisecond)->Complexity(benchmark::oAuto);