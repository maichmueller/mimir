from __future__ import annotations

import argparse
import time
from pathlib import Path

import pymimir.advanced.search as search

ROOT_DIR = Path(__file__).resolve().parents[2]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Benchmark IW beam search with 1/2/4/8 worker threads."
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
    parser.add_argument(
        "--chunk-sizes",
        type=int,
        nargs="+",
        default=[1024],
        help="Parallel beam chunk sizes to benchmark.",
    )
    parser.add_argument(
        "--beam-novelty-mode",
        choices=["survivors_only", "all_tested"],
        default="survivors_only",
        help="Beam novelty mode to benchmark.",
    )
    parser.add_argument(
        "--mode",
        choices=["grounded", "lifted", "lifted_symmetry_pruning", "lifted_exhaustive"],
        default="grounded",
        help="Search context mode to benchmark.",
    )
    parser.add_argument(
        "--relaxed-survivors-only-beam",
        action="store_true",
        help="Use the relaxed parallel survivors-only beam mode. Serial references are still measured on the deterministic serial path.",
    )
    return parser.parse_args()


def run_once(
    domain_filepath: str,
    problem_filepath: str,
    max_arity: int,
    beam_width: int,
    num_threads: int,
    beam_novelty_mode: str,
    chunk_size: int,
    mode: str,
    relaxed_survivors_only_beam: bool,
) -> dict[str, float | int | str]:
    if mode == "grounded":
        search_mode = search.GroundedOptions()
    elif mode == "lifted":
        search_mode = search.LiftedOptions(search.LiftedKPKCOptions(search.SymmetryPruning.OFF))
    elif mode == "lifted_symmetry_pruning":
        search_mode = search.LiftedOptions(search.LiftedKPKCOptions(search.SymmetryPruning.GI))
    else:
        search_mode = search.LiftedOptions(search.LiftedExhaustiveOptions())

    search_context = search.SearchContext.create(domain_filepath, problem_filepath, search.SearchContextOptions(search_mode))
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
    options.beam_novelty_mode = (
        search.BeamNoveltyMode.SURVIVORS_ONLY
        if beam_novelty_mode == "survivors_only"
        else search.BeamNoveltyMode.ALL_TESTED
    )
    options.max_arity = max_arity
    options.relaxed_survivors_only_beam = relaxed_survivors_only_beam
    if num_threads > 1:
        options.parallel_beam_num_threads = num_threads
    if chunk_size > 0:
        options.parallel_beam_chunk_size = chunk_size

    wall_start = time.perf_counter()
    result = search.find_solution_iw(search_context, options)
    wall_time_ms = (time.perf_counter() - wall_start) * 1000.0
    iw_stats = iw_event_handler.get_statistics()
    brfs_stats = iw_stats.get_brfs_statistics_by_arity()
    generated = sum(stats.get_num_generated() for stats in brfs_stats)
    chunk_flushes = sum(stats.get_num_parallel_beam_chunk_flushes() for stats in brfs_stats)
    chunk_tasks_total = sum(stats.get_num_parallel_beam_chunk_tasks_total() for stats in brfs_stats)
    max_chunk_size = max((stats.get_max_parallel_beam_chunk_size() for stats in brfs_stats), default=0)
    avg_chunk_size = (chunk_tasks_total / chunk_flushes) if chunk_flushes else 0.0
    worker_compute_ms = sum(stats.get_parallel_beam_worker_compute_time_ms() for stats in brfs_stats)
    main_merge_ms = sum(stats.get_parallel_beam_main_thread_merge_time_ms() for stats in brfs_stats)
    main_intern_ms = sum(stats.get_parallel_beam_main_thread_intern_time_ms() for stats in brfs_stats)
    fluent_slot_ms = sum(stats.get_parallel_beam_fluent_slot_time_ms() for stats in brfs_stats)
    numeric_slot_ms = sum(stats.get_parallel_beam_numeric_slot_time_ms() for stats in brfs_stats)
    derived_slot_ms = sum(stats.get_parallel_beam_derived_slot_time_ms() for stats in brfs_stats)
    state_lookup_ms = sum(stats.get_parallel_beam_state_lookup_time_ms() for stats in brfs_stats)
    reached_atoms_ms = sum(stats.get_parallel_beam_reached_atom_update_time_ms() for stats in brfs_stats)
    ready_queue_hwm = max((stats.get_parallel_beam_ready_queue_high_water() for stats in brfs_stats), default=0)
    in_flight_hwm = max((stats.get_parallel_beam_in_flight_chunks_high_water() for stats in brfs_stats), default=0)
    consumer_stall_ms = sum(stats.get_parallel_beam_consumer_stall_time_ms() for stats in brfs_stats)
    producer_stall_ms = sum(stats.get_parallel_beam_producer_stall_time_ms() for stats in brfs_stats)
    plan_length = len(result.plan.get_actions()) if result.plan else 0
    search_time_ms = iw_stats.get_search_time_ms().total_seconds() * 1000.0

    return {
        "status": result.status.name.lower(),
        "plan_length": plan_length,
        "generated": generated,
        "search_time_ms": search_time_ms,
        "wall_time_ms": wall_time_ms,
        "chunk_flushes": chunk_flushes,
        "avg_chunk": avg_chunk_size,
        "max_chunk": max_chunk_size,
        "worker_ms": worker_compute_ms,
        "merge_ms": main_merge_ms,
        "intern_ms": main_intern_ms,
        "fluent_slot_ms": fluent_slot_ms,
        "numeric_slot_ms": numeric_slot_ms,
        "derived_slot_ms": derived_slot_ms,
        "lookup_ms": state_lookup_ms,
        "reached_ms": reached_atoms_ms,
        "ready_hwm": ready_queue_hwm,
        "in_flight_hwm": in_flight_hwm,
        "consumer_stall_ms": consumer_stall_ms,
        "producer_stall_ms": producer_stall_ms,
    }


if __name__ == "__main__":
    args = parse_args()
    for chunk_size in args.chunk_sizes:
        serial_reference = run_once(
            args.domain,
            args.problem,
            args.max_arity,
            args.beam_width,
            1,
            args.beam_novelty_mode,
            chunk_size,
            args.mode,
            False,
        )
        print(f"chunk_size={chunk_size:>4} serial_reference_ms={serial_reference['wall_time_ms']:>8.2f}")
        for num_threads in args.threads:
            if args.relaxed_survivors_only_beam and num_threads <= 1:
                print(
                    f"search_mode={args.mode:>24} novelty={args.beam_novelty_mode:>14} threads={num_threads:>2} chunk_size={chunk_size:>4} "
                    "skipped=true reason=relaxed_survivors_only_beam_requires_parallel_threads "
                    f"serial_reference_ms={serial_reference['wall_time_ms']:>8.2f}"
                )
                continue

            stats = run_once(
                args.domain,
                args.problem,
                args.max_arity,
                args.beam_width,
                num_threads,
                args.beam_novelty_mode,
                chunk_size,
                args.mode,
                args.relaxed_survivors_only_beam,
            )
            print(
                f"search_mode={args.mode:>24} novelty={args.beam_novelty_mode:>14} threads={num_threads:>2} chunk_size={chunk_size:>4} "
                f"relaxed={str(args.relaxed_survivors_only_beam).lower():>5} status={stats['status']:>10} "
                f"plan_length={stats['plan_length']:>2} generated={stats['generated']:>6} "
                f"search_time_ms={stats['search_time_ms']:>8.2f} wall_time_ms={stats['wall_time_ms']:>8.2f} "
                f"speedup_vs_serial={serial_reference['wall_time_ms'] / stats['wall_time_ms']:>6.2f} "
                f"chunk_flushes={stats['chunk_flushes']:>4} avg_chunk={stats['avg_chunk']:>8.2f} max_chunk={stats['max_chunk']:>4} "
                f"worker_ms={stats['worker_ms']:>8.2f} merge_ms={stats['merge_ms']:>8.2f} intern_ms={stats['intern_ms']:>8.2f} "
                f"fluent_slot_ms={stats['fluent_slot_ms']:>8.2f} numeric_slot_ms={stats['numeric_slot_ms']:>8.2f} "
                f"derived_slot_ms={stats['derived_slot_ms']:>8.2f} lookup_ms={stats['lookup_ms']:>8.2f} reached_ms={stats['reached_ms']:>8.2f} "
                f"ready_hwm={stats['ready_hwm']:>2} in_flight_hwm={stats['in_flight_hwm']:>2} "
                f"consumer_stall_ms={stats['consumer_stall_ms']:>8.2f} producer_stall_ms={stats['producer_stall_ms']:>8.2f}"
            )
