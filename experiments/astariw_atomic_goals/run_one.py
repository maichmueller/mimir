"""Worker: run exactly ONE (problem, single-atom goal, mode, width) unit and exit.

One process per unit is what makes the comparison fair: the process owns the
parser, the grounded problem, the state repository and the search tree, and it
dies right after reporting, so no structure built for one goal or one mode can
be reused (or can inflate the peak RSS) of the next.

Emits a single JSON object on stdout.
"""

from __future__ import annotations

import argparse
import gc
import json
import os
import resource
import sys
import time


def rss_kb() -> int:
    """Current resident set size in KiB (Linux)."""
    with open("/proc/self/statm") as f:
        pages = int(f.read().split()[1])
    return pages * os.sysconf("SC_PAGE_SIZE") // 1024


def as_ms(v) -> float:
    """Statistics timers come back as datetime.timedelta; normalise to float ms."""
    try:
        return v.total_seconds() * 1000.0
    except AttributeError:
        return float(v)


def peak_rss_kb() -> int:
    """Peak RSS in KiB (ru_maxrss is already KiB on Linux)."""
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--domain", required=True)
    ap.add_argument("--problem", required=True)
    ap.add_argument("--mode", required=True, choices=["iw", "astar_iw"])
    ap.add_argument("--width", type=int, required=True)
    ap.add_argument("--max-time-ms", type=int, default=30_000)
    ap.add_argument("--max-states", type=int, default=2_000_000)
    ap.add_argument("--novelty-mode", default="CLASSICAL")
    ap.add_argument("--heuristic", default="blind", choices=["blind", "ff", "max", "add"])
    ap.add_argument("--context", default="grounded", choices=["grounded", "lifted"])
    ap.add_argument("--heuristic-weight", type=float, default=1.0)
    args = ap.parse_args()

    rec: dict = {
        "mode": args.mode,
        "width": args.width,
        "domain_file": args.domain,
        "problem_file": args.problem,
        "heuristic": args.heuristic,
        "context": args.context,
    }

    t_import0 = time.perf_counter()
    import pymimir.advanced.search as search  # noqa: E402

    rec["import_s"] = time.perf_counter() - t_import0
    rec["rss_after_import_kb"] = rss_kb()

    # ---- parse + ground -------------------------------------------------
    t0 = time.perf_counter()
    if args.context == "lifted":
        ctx_options = search.SearchContextOptions(search.LiftedOptions())
    else:
        ctx_options = search.SearchContextOptions(search.GroundedOptions())
    ctx = search.SearchContext.create(args.domain, args.problem, ctx_options)
    rec["setup_s"] = time.perf_counter() - t0
    rec["rss_after_setup_kb"] = rss_kb()
    rec["peak_rss_after_setup_kb"] = peak_rss_kb()

    problem = ctx.get_problem()

    # ---- search ---------------------------------------------------------
    if args.mode == "iw":
        opts = search.IWOptions()
        opts.max_arity = args.width
        opts.max_time_in_ms = args.max_time_ms
        opts.max_num_states = args.max_states
        handler = search.DefaultIWEventHandler.create(problem, True)
        opts.iw_event_handler = handler
        t1 = time.perf_counter()
        result = search.find_solution_iw(ctx, opts)
        wall = time.perf_counter() - t1
        stats = handler.get_statistics()
        rec["search_time_ms_internal"] = as_ms(stats.get_search_time_ms())
        rec["effective_width"] = stats.get_effective_width()
        per_arity = []
        exp = gen = nodes = states = 0
        for arity, st in enumerate(stats.get_brfs_statistics_by_arity()):
            per_arity.append(
                {
                    "arity": arity,
                    "expanded": st.get_num_expanded(),
                    "generated": st.get_num_generated(),
                    "nodes": st.get_num_nodes(),
                    "states": st.get_num_states(),
                    "pruned": st.get_num_pruned(),
                    "deadends": st.get_num_deadends(),
                    "time_ms": as_ms(st.get_search_time_ms()),
                }
            )
            exp += st.get_num_expanded()
            gen += st.get_num_generated()
            nodes += st.get_num_nodes()
            states = max(states, st.get_num_states())
        rec["per_arity"] = per_arity
        rec["num_expanded"] = exp
        rec["num_generated"] = gen
        rec["num_nodes"] = nodes
        rec["num_states"] = states
    else:
        t_h = time.perf_counter()
        if args.heuristic == "blind":
            heuristic = search.BlindHeuristic.create(problem)
        else:
            # A lifted delete-relaxed explorator, so the heuristic never forces
            # the full grounding the lifted context is there to avoid.
            grounder = search.LiftedGrounder(problem)
            heuristic = {
                "ff": search.FFHeuristic,
                "max": search.MaxHeuristic,
                "add": search.AddHeuristic,
            }[args.heuristic].create(grounder)
        rec["heuristic_setup_s"] = time.perf_counter() - t_h
        opts = search.AStarIWOptions()
        opts.width = args.width
        opts.novelty_feature_mode = getattr(search.AStarIWNoveltyFeatureMode, args.novelty_mode)
        opts.heuristic_weight = args.heuristic_weight
        opts.max_time_in_ms = args.max_time_ms
        opts.max_num_states = args.max_states
        handler = search.DefaultAStarIWEventHandler.create(problem, True)
        opts.event_handler = handler
        t1 = time.perf_counter()
        result = search.find_solution_astar_iw(ctx, heuristic, opts)
        wall = time.perf_counter() - t1
        stats = handler.get_statistics()
        rec["search_time_ms_internal"] = as_ms(stats.get_search_time_ms())
        rec["num_expanded"] = stats.get_num_expanded()
        rec["num_generated"] = stats.get_num_generated()
        rec["num_nodes"] = stats.get_num_nodes()
        rec["num_states"] = stats.get_num_states()
        rec["num_deadends"] = stats.get_num_deadends()
        rec["num_novelty_rejected"] = stats.get_num_novelty_rejected()
        rec["num_reopened"] = stats.get_num_reopened()
        rec["num_stale_g_discarded"] = stats.get_num_stale_g_discarded()
        rec["num_stale_novelty_discarded"] = stats.get_num_stale_novelty_discarded()

    rec["search_wall_s"] = wall
    rec["total_wall_s"] = rec["setup_s"] + wall
    rec["peak_rss_kb"] = peak_rss_kb()
    rec["rss_after_search_kb"] = rss_kb()
    rec["status"] = str(result.status).rsplit(".", 1)[-1]

    plan = result.plan
    if plan is not None:
        actions = [a.to_string_for_plan(problem) for a in plan.get_actions()]
        rec["plan_length"] = len(actions)
        rec["plan_cost"] = float(plan.get_cost())
        rec["plan"] = actions
    else:
        rec["plan_length"] = None
        rec["plan_cost"] = None
        rec["plan"] = None

    # Drop everything before reporting so the teardown cost is not attributed
    # to some later measurement; the process exits immediately afterwards.
    del result, plan, opts, handler, stats, problem, ctx
    gc.collect()

    print(json.dumps(rec))
    return 0


if __name__ == "__main__":
    sys.exit(main())
