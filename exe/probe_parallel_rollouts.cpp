/*
 * Diagnostic probe for the K-parallel-rollout design investigation.
 *
 * Answers three questions empirically:
 *   1. Which `ProblemImpl`-level repositories actually grow *during* a search
 *      (as opposed to during parse/ground)? Reported per repository as a delta.
 *   2. How much do the `valla` interning tables (index tree / double leaf) grow
 *      per rollout? These are the only per-successor shared writes.
 *   3. Does giving each rollout its own `SearchContext` (private
 *      `StateRepository` + private match-tree-backed generators) over one shared
 *      `Problem` produce identical results serially, and does it scale in
 *      wall-clock when run on real threads?
 *
 * This is a diagnostic tool, not a test. It intentionally uses raw std::thread
 * so that TSAN sees the plain data races if there are any.
 */

#include <argparse/argparse.hpp>
#include <chrono>
#include <iostream>
#include <mimir/mimir.hpp>
#include <thread>
#include <vector>

using namespace mimir;
using namespace mimir::search;
using namespace mimir::formalism;

namespace
{

struct RepoSizes
{
    std::vector<std::pair<std::string, size_t>> entries;
};

/// @brief Snapshot the size of every repository in the problem's `Repositories`.
RepoSizes snapshot_repositories(const ProblemImpl& problem)
{
    auto result = RepoSizes {};
    boost::hana::for_each(problem.get_repositories().get_hana_repositories(),
                          [&](auto&& pair)
                          {
                              using KeyT = typename std::decay_t<decltype(boost::hana::first(pair))>::type;
                              result.entries.emplace_back(typeid(KeyT).name(), boost::hana::second(pair).size());
                          });
    return result;
}

void report_repository_delta(const RepoSizes& before, const RepoSizes& after, const std::string& label)
{
    std::cout << "  [" << label << "] Repositories deltas (only non-zero shown):\n";
    auto any = false;
    for (size_t i = 0; i < before.entries.size(); ++i)
    {
        const auto delta = after.entries[i].second - before.entries[i].second;
        if (delta != 0)
        {
            any = true;
            std::cout << "    " << before.entries[i].first << ": " << before.entries[i].second << " -> " << after.entries[i].second << "  (+" << delta
                      << ")\n";
        }
    }
    if (!any)
    {
        std::cout << "    <none: Repositories is frozen during this phase>\n";
    }
}

struct VallaSizes
{
    size_t index_tree;
    size_t double_leaf;
};

VallaSizes snapshot_valla(const ProblemImpl& problem) { return { problem.get_index_tree_table().size(), problem.get_double_leaf_table().size() }; }

/// @brief Run one stochastic IW(1) rollout on a private search context.
struct RolloutResult
{
    size_t num_states;
    size_t num_reached_fluent_atoms;
    SearchStatus status;
};

RolloutResult run_rollout(const SearchContext& context, uint64_t seed, uint32_t max_depth)
{
    auto options = iw::Options();
    options.max_arity = 1;
    options.layer_ordering_strategy = RandomizedLayerOrderingStrategyImpl::create(seed);
    options.randomize_equal_score_ties = true;
    options.equal_score_tie_seed = seed;
    if (max_depth > 0)
    {
        options.max_depth = max_depth;
    }
    options.iw_event_handler = iw::DefaultEventHandlerImpl::create(context->get_problem(), true);
    options.brfs_event_handler = brfs::DefaultEventHandlerImpl::create(context->get_problem(), true);

    const auto result = iw::find_solution(context, options);

    const auto& repo = *context->get_state_repository();
    return { repo.get_state_count(), repo.get_reached_fluent_ground_atoms_bitset().count(), result.status };
}

}

int main(int argc, char** argv)
{
    auto program = argparse::ArgumentParser("probe_parallel_rollouts");
    program.add_argument("-D", "--domain-filepath").required();
    program.add_argument("-P", "--problem-filepath").required();
    program.add_argument("-K", "--num-rollouts").default_value(size_t(16)).scan<'u', size_t>();
    program.add_argument("--max-depth").default_value(uint32_t(0)).scan<'u', uint32_t>();
    program.add_argument("--threads").default_value(size_t(0)).scan<'u', size_t>().help("0 = K threads");

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
    const auto num_rollouts = program.get<size_t>("--num-rollouts");
    const auto max_depth = program.get<uint32_t>("--max-depth");
    const auto num_threads = program.get<size_t>("--threads") == 0 ? num_rollouts : program.get<size_t>("--threads");

    /* Phase 0: parse + ground once. */
    const auto parse_start = std::chrono::steady_clock::now();
    const auto problem = ProblemImpl::create(domain_filepath, problem_filepath);
    const auto parse_end = std::chrono::steady_clock::now();

    const auto ground_start = std::chrono::steady_clock::now();
    const auto base_context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
    const auto ground_end = std::chrono::steady_clock::now();

    std::cout << "Problem: " << problem->get_name() << "\n"
              << "  parse:  " << std::chrono::duration_cast<std::chrono::milliseconds>(parse_end - parse_start).count() << " ms\n"
              << "  ground: " << std::chrono::duration_cast<std::chrono::milliseconds>(ground_end - ground_start).count() << " ms\n";

    const auto repos_after_ground = snapshot_repositories(*problem);
    const auto valla_after_ground = snapshot_valla(*problem);
    std::cout << "  valla index_tree after grounding: " << valla_after_ground.index_tree << "\n"
              << "  valla double_leaf after grounding: " << valla_after_ground.double_leaf << "\n";

    /* Phase 1: does creating an extra SearchContext from the same Problem mutate Repositories? */
    {
        const auto before = snapshot_repositories(*problem);
        const auto valla_before = snapshot_valla(*problem);
        const auto extra_ctx_start = std::chrono::steady_clock::now();
        const auto extra_context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
        const auto extra_ctx_end = std::chrono::steady_clock::now();
        const auto after = snapshot_repositories(*problem);
        const auto valla_after = snapshot_valla(*problem);

        std::cout << "\n== Phase 1: second SearchContext from the same Problem ==\n"
                  << "  time: " << std::chrono::duration_cast<std::chrono::milliseconds>(extra_ctx_end - extra_ctx_start).count() << " ms\n";
        report_repository_delta(before, after, "extra SearchContext");
        std::cout << "  valla index_tree: +" << (valla_after.index_tree - valla_before.index_tree) << ", double_leaf: +"
                  << (valla_after.double_leaf - valla_before.double_leaf) << "\n";
    }

    /* Phase 2: one rollout on a private context; what grows? */
    {
        const auto context = SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
        const auto before = snapshot_repositories(*problem);
        const auto valla_before = snapshot_valla(*problem);
        const auto t0 = std::chrono::steady_clock::now();
        const auto rollout = run_rollout(context, 0, max_depth);
        const auto t1 = std::chrono::steady_clock::now();
        const auto after = snapshot_repositories(*problem);
        const auto valla_after = snapshot_valla(*problem);

        std::cout << "\n== Phase 2: single IW(1) rollout (seed 0) ==\n"
                  << "  time: " << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << " ms, states: " << rollout.num_states
                  << ", reached fluent atoms: " << rollout.num_reached_fluent_atoms << "\n";
        report_repository_delta(before, after, "single rollout");
        std::cout << "  valla index_tree: +" << (valla_after.index_tree - valla_before.index_tree) << ", double_leaf: +"
                  << (valla_after.double_leaf - valla_before.double_leaf) << "\n";
    }

    /* Phase 3: K rollouts serially, each on its own private context. */
    auto serial_results = std::vector<RolloutResult> {};
    auto serial_ms = int64_t(0);
    {
        auto contexts = std::vector<SearchContext> {};
        for (size_t k = 0; k < num_rollouts; ++k)
        {
            contexts.push_back(SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions())));
        }
        const auto before = snapshot_repositories(*problem);
        const auto valla_before = snapshot_valla(*problem);
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t k = 0; k < num_rollouts; ++k)
        {
            serial_results.push_back(run_rollout(contexts[k], k, max_depth));
        }
        const auto t1 = std::chrono::steady_clock::now();
        const auto after = snapshot_repositories(*problem);
        const auto valla_after = snapshot_valla(*problem);
        serial_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        std::cout << "\n== Phase 3: K=" << num_rollouts << " rollouts, serial, private contexts ==\n"
                  << "  time: " << serial_ms << " ms\n";
        report_repository_delta(before, after, "K serial rollouts");
        std::cout << "  valla index_tree: +" << (valla_after.index_tree - valla_before.index_tree) << ", double_leaf: +"
                  << (valla_after.double_leaf - valla_before.double_leaf) << "\n";
    }

    /* Phase 4: K rollouts on real threads, each on its own private context. */
    {
        auto contexts = std::vector<SearchContext> {};
        for (size_t k = 0; k < num_rollouts; ++k)
        {
            contexts.push_back(SearchContextImpl::create(problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions())));
        }
        auto parallel_results = std::vector<RolloutResult>(num_rollouts);

        const auto before = snapshot_repositories(*problem);
        const auto valla_before = snapshot_valla(*problem);
        const auto t0 = std::chrono::steady_clock::now();
        {
            auto threads = std::vector<std::thread> {};
            auto next = std::atomic<size_t> { 0 };
            for (size_t t = 0; t < num_threads; ++t)
            {
                threads.emplace_back(
                    [&]
                    {
                        for (auto k = next.fetch_add(1); k < num_rollouts; k = next.fetch_add(1))
                        {
                            parallel_results[k] = run_rollout(contexts[k], k, max_depth);
                        }
                    });
            }
            for (auto& thread : threads)
            {
                thread.join();
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        const auto after = snapshot_repositories(*problem);
        const auto valla_after = snapshot_valla(*problem);
        const auto parallel_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        std::cout << "\n== Phase 4: K=" << num_rollouts << " rollouts, " << num_threads << " threads, private contexts ==\n"
                  << "  time: " << parallel_ms << " ms  (serial was " << serial_ms << " ms, speedup "
                  << (parallel_ms > 0 ? static_cast<double>(serial_ms) / static_cast<double>(parallel_ms) : 0.0) << "x)\n";
        report_repository_delta(before, after, "K parallel rollouts");
        std::cout << "  valla index_tree: +" << (valla_after.index_tree - valla_before.index_tree) << ", double_leaf: +"
                  << (valla_after.double_leaf - valla_before.double_leaf) << "\n";

        auto mismatches = size_t(0);
        for (size_t k = 0; k < num_rollouts; ++k)
        {
            if (parallel_results[k].num_states != serial_results[k].num_states
                || parallel_results[k].num_reached_fluent_atoms != serial_results[k].num_reached_fluent_atoms
                || parallel_results[k].status != serial_results[k].status)
            {
                ++mismatches;
                std::cout << "    MISMATCH seed " << k << ": serial(states=" << serial_results[k].num_states
                          << ", atoms=" << serial_results[k].num_reached_fluent_atoms << ") vs parallel(states=" << parallel_results[k].num_states
                          << ", atoms=" << parallel_results[k].num_reached_fluent_atoms << ")\n";
            }
        }
        std::cout << "  equivalence vs serial: " << (mismatches == 0 ? "ALL MATCH" : std::to_string(mismatches) + " MISMATCHES") << "\n";
    }

    /* Phase 5: K rollouts on real threads, each on a *fully independent* Problem.
       This is the "each thread gets a private namespace" extreme. If this scales
       while phase 4 does not, the shared `ProblemImpl`-level valla interning
       tables are the sole source of the slowdown. */
    {
        const auto parse_all_start = std::chrono::steady_clock::now();
        auto private_problems = std::vector<Problem> {};
        auto private_contexts = std::vector<SearchContext> {};
        for (size_t k = 0; k < num_rollouts; ++k)
        {
            private_problems.push_back(ProblemImpl::create(domain_filepath, problem_filepath));
            private_contexts.push_back(
                SearchContextImpl::create(private_problems.back(), SearchContextImpl::Options(SearchContextImpl::GroundedOptions())));
        }
        const auto parse_all_end = std::chrono::steady_clock::now();

        auto private_results = std::vector<RolloutResult>(num_rollouts);
        const auto t0 = std::chrono::steady_clock::now();
        {
            auto threads = std::vector<std::thread> {};
            auto next = std::atomic<size_t> { 0 };
            for (size_t t = 0; t < num_threads; ++t)
            {
                threads.emplace_back(
                    [&]
                    {
                        for (auto k = next.fetch_add(1); k < num_rollouts; k = next.fetch_add(1))
                        {
                            private_results[k] = run_rollout(private_contexts[k], k, max_depth);
                        }
                    });
            }
            for (auto& thread : threads)
            {
                thread.join();
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        const auto private_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        std::cout << "\n== Phase 5: K=" << num_rollouts << " rollouts, " << num_threads << " threads, FULLY PRIVATE Problems ==\n"
                  << "  K x (parse+ground) setup cost: " << std::chrono::duration_cast<std::chrono::milliseconds>(parse_all_end - parse_all_start).count()
                  << " ms\n"
                  << "  search time: " << private_ms << " ms  (serial was " << serial_ms << " ms, speedup "
                  << (private_ms > 0 ? static_cast<double>(serial_ms) / static_cast<double>(private_ms) : 0.0) << "x)\n";

        auto mismatches = size_t(0);
        for (size_t k = 0; k < num_rollouts; ++k)
        {
            if (private_results[k].num_states != serial_results[k].num_states
                || private_results[k].num_reached_fluent_atoms != serial_results[k].num_reached_fluent_atoms)
            {
                ++mismatches;
            }
        }
        std::cout << "  equivalence vs serial: " << (mismatches == 0 ? "ALL MATCH" : std::to_string(mismatches) + " MISMATCHES") << "\n";
    }

    /* Phase 6: the shipped batched API -- shared Problem, shared generators, private
       state repositories with private interning tables. */
    {
        const auto fresh_problem = ProblemImpl::create(domain_filepath, problem_filepath);
        const auto setup_start = std::chrono::steady_clock::now();
        const auto fresh_context = SearchContextImpl::create(fresh_problem, SearchContextImpl::Options(SearchContextImpl::GroundedOptions()));
        const auto setup_end = std::chrono::steady_clock::now();

        auto batch_options = iw::ParallelRolloutOptions();
        batch_options.num_threads = static_cast<uint32_t>(num_threads);
        batch_options.options.max_arity = 1;
        if (max_depth > 0)
        {
            batch_options.options.max_depth = max_depth;
        }
        for (size_t k = 0; k < num_rollouts; ++k)
        {
            batch_options.seeds.push_back(k);
        }

        const auto t0 = std::chrono::steady_clock::now();
        const auto batch = iw::find_rollouts_parallel(fresh_context, batch_options);
        const auto t1 = std::chrono::steady_clock::now();

        /* Serial reference over the same fresh problem, same seeds. */
        auto serial_batch_options = batch_options;
        serial_batch_options.num_threads = 1;
        const auto t2 = std::chrono::steady_clock::now();
        const auto serial_batch = iw::find_rollouts_parallel(fresh_context, serial_batch_options);
        const auto t3 = std::chrono::steady_clock::now();

        const auto parallel_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        const auto serial_batch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

        std::cout << "\n== Phase 6: batched API (find_rollouts_parallel), K=" << num_rollouts << ", " << num_threads << " threads ==\n"
                  << "  one-time setup (parse excluded, grounding + match tree): "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(setup_end - setup_start).count() << " ms\n"
                  << "  parallel: " << parallel_ms << " ms;  serial (same API, 1 thread): " << serial_batch_ms << " ms;  speedup "
                  << (parallel_ms > 0 ? static_cast<double>(serial_batch_ms) / static_cast<double>(parallel_ms) : 0.0) << "x\n";

        auto mismatches = size_t(0);
        for (size_t k = 0; k < num_rollouts; ++k)
        {
            if (batch[k].num_states != serial_batch[k].num_states || batch[k].status != serial_batch[k].status
                || batch[k].reached_fluent_atoms != serial_batch[k].reached_fluent_atoms
                || batch[k].reached_derived_atoms != serial_batch[k].reached_derived_atoms)
            {
                ++mismatches;
                std::cout << "    MISMATCH seed " << k << ": serial(states=" << serial_batch[k].num_states
                          << ", atoms=" << serial_batch[k].reached_fluent_atoms.count() << ") vs parallel(states=" << batch[k].num_states
                          << ", atoms=" << batch[k].reached_fluent_atoms.count() << ")\n";
            }
        }
        std::cout << "  parallel vs serial equivalence: " << (mismatches == 0 ? "ALL MATCH" : std::to_string(mismatches) + " MISMATCHES") << "\n";

        auto intersection = batch.front().reached_fluent_atoms;
        for (const auto& rollout : batch)
        {
            intersection &= rollout.reached_fluent_atoms;
        }
        std::cout << "  reached-atom intersection over the batch: " << intersection.count() << " atoms (per-rollout: "
                  << batch.front().reached_fluent_atoms.count() << ")\n";
    }

    return 0;
}
