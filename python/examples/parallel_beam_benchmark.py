from __future__ import annotations

import argparse
import time
from pathlib import Path

import pymimir.advanced.search as search

ROOT_DIR = Path(__file__).resolve().parents[2]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Benchmark grounded IW beam search with 1/2/4/8 worker threads."
    )
    parser.add_argument(
        "--domain",
        default=str(ROOT_DIR / "data" / "delivery" / "domain.pddl"),
        help="Path to the domain PDDL file.",
    )
    parser.add_argument(
        "--problem",
        default=str(ROOT_DIR / "data" / "delivery" / "test_problem.pddl"),
        help="Path to the problem PDDL file.",
    )
    parser.add_argument(
        "--beam-width",
        type=int,
        default=32,
        help="Beam width for the IW beam run.",
    )
    parser.add_argument(
        "--max-arity",
        type=int,
        default=2,
        help="Maximum IW arity.",
    )
    parser.add_argument(
        "--threads",
        type=int,
        nargs="+",
        default=[1, 2, 4, 8],
        help="Thread counts to benchmark. Values <= 1 use the serial beam path.",
    )
    return parser.parse_args()


def run_once(domain_filepath: str, problem_filepath: str, max_arity: int, beam_width: int, num_threads: int) -> None:
    search_context = search.SearchContext.create(domain_filepath, problem_filepath, search.SearchContextOptions())
    problem = search_context.get_problem()
    start_state, _ = search_context.get_state_repository().get_or_create_initial_state()

    iw_event_handler = search.DefaultIWEventHandler(problem, quiet=True)
    brfs_event_handler = search.DefaultBrFSEventHandler(problem, quiet=True)
    options = search.IWOptions()
    options.start_state = start_state
    options.iw_event_handler = iw_event_handler
    options.brfs_event_handler = brfs_event_handler
    options.layer_ordering_strategy = search.GoalCountLayerOrderingStrategy(problem)
    options.beam_width = beam_width
    options.beam_novelty_mode = search.BeamNoveltyMode.SURVIVORS_ONLY
    options.max_arity = max_arity
    if num_threads > 1:
        options.parallel_beam_num_threads = num_threads

    wall_start = time.perf_counter()
    result = search.find_solution_iw(search_context, options)
    wall_time_ms = (time.perf_counter() - wall_start) * 1000.0
    iw_stats = iw_event_handler.get_statistics()
    generated = sum(stats.get_num_generated() for stats in iw_stats.get_brfs_statistics_by_arity())
    plan_length = len(result.plan.get_actions()) if result.plan else 0

    print(
        f"threads={num_threads:>2} status={result.status.name.lower():>10} "
        f"plan_length={plan_length:>2} generated={generated:>6} "
        f"search_time_ms={iw_stats.get_search_time_ms():>8} wall_time_ms={wall_time_ms:>8.2f}"
    )


if __name__ == "__main__":
    args = parse_args()
    for num_threads in args.threads:
        run_once(args.domain, args.problem, args.max_arity, args.beam_width, num_threads)
