#!/usr/bin/env python3
"""Compare LIW(k) against IW(k) on atomic goals drawn from IPC problems.

Each conjunct of a problem's fluent goal becomes one atomic goal, and every configuration is run
on every atomic goal. Atomic goals are the right unit here: a full IPC conjunctive goal is far
beyond width 1 or 2, so running IW(1) on it only ever measures how fast it fails, whereas single
conjuncts are exactly the regime where width matters and where the IW(1)/IW(2) boundary -- which
is what LIW(k) is meant to sit inside -- actually falls.

All configurations for one problem share a single grounded instance. That is not an optimization:
two Problem instances of the same PDDL can enumerate grounded actions in different orders, and
under novelty pruning generation order decides which candidate claims a contested tuple, so
expansion counts only compare meaningfully within one grounded instance.

Needs a `pymimir` build that has `IWOptions.landmark_novelty_graph`; point PYTHONPATH at it if the
installed package predates LIW.

Usage:
    python experiments/liw_atomic_goals.py --out results.jsonl -j 8
    python experiments/liw_atomic_goals.py --domains blocksworld-ipc ferry-ipc --limit 5
    python experiments/liw_atomic_goals.py --include-iw3          # see the note on IW(3) below

Then summarize with `liw_atomic_goals_report.py`.

Note when reading results: under a per-search time cap, a configuration with weaker pruning can
lose a goal it would eventually have solved. In the first run over the IPC test split, every goal
LIW lost to IW was OUT_OF_TIME and never EXHAUSTED -- LIW does not lose on pruning power, only on
wall clock -- so the cap is part of the measurement and worth varying.
"""

from __future__ import annotations

import argparse
import json
import multiprocessing as mp
import os
import queue
import sys
import time
from dataclasses import dataclass, asdict
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
DEFAULT_DATA_DIR = ROOT_DIR / "data" / "ipc"


# ---------------------------------------------------------------------------------------------
# Configurations
# ---------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Config:
    name: str
    width: int
    landmarks: bool

    def predicted_table_bytes(self, num_atoms: int, num_landmarks: int) -> int:
        """Rough worst-case novelty table footprint, used to skip runs that would thrash.

        IW(k) uses the dense minimum-g table: one rank byte per free tuple, so (N+1)^k bytes.
        LIW(k) uses the sparse landmark table, whose size is bounded by what the search actually
        reaches rather than by the tuple space, so it is reported as zero here and never skipped.
        """
        if self.landmarks:
            return 0
        return (num_atoms + 1) ** self.width


BASE_CONFIGS = [
    Config("IW(1)", 1, False),
    Config("LIW(1)", 1, True),
    Config("IW(2)", 2, False),
    Config("LIW(2)", 2, True),
]

# IW(3)'s dense minimum-g table is (N+1)^3 bytes -- 1 GB at only 1000 ground atoms -- so it is off
# by default and still subject to --max-table-bytes when enabled. LIW(2) is the intended
# stand-in: it reaches into width-3 territory on a table that stays linear in the landmark count.
IW3_CONFIG = Config("IW(3)", 3, False)


@dataclass
class Row:
    domain: str
    split: str
    problem: str
    goal_index: int
    goal_atom: str
    config: str
    status: str
    plan_length: int
    num_expanded: int
    num_generated: int
    num_novelty_rejected: int
    search_time_ms: float
    wall_time_ms: float
    num_landmarks: int
    num_fluent_atoms: int
    note: str = ""


# ---------------------------------------------------------------------------------------------
# Worker
# ---------------------------------------------------------------------------------------------


def _atom_to_string(atom) -> str:
    objects = ",".join(obj.get_name() for obj in atom.get_objects())
    return f"{atom.get_predicate().get_name()}({objects})"


def run_problem(domain_dir: str, problem_file: str, opts: dict) -> list[dict]:
    """Run every configuration on every atomic goal of one problem. Executed in a child process."""
    import pymimir.advanced.formalism as formalism
    import pymimir.advanced.search as search

    # `domain_dir` is the split directory (".../<domain>/train"), so the domain is its parent.
    domain_name = Path(domain_dir).parent.name
    split_name = Path(domain_dir).name
    problem_name = Path(problem_file).name

    configs = list(BASE_CONFIGS)
    if opts["include_iw3"]:
        configs.append(IW3_CONFIG)

    rows: list[Row] = []

    problem = formalism.Problem.create(
        str(Path(domain_dir) / "domain.pddl"), str(problem_file), formalism.ParserOptions()
    )
    grounder = search.LiftedGrounder(problem)
    match_tree_options = search.MatchTreeOptions()
    axiom_evaluator = grounder.create_grounded_axiom_evaluator(match_tree_options)
    state_repository = search.StateRepository.create(axiom_evaluator)
    action_generator = grounder.create_grounded_applicable_action_generator(match_tree_options)
    context = search.SearchContext.create(problem, action_generator, state_repository)

    landmark_graph = search.ApproximateFactLandmarkGenerator.create(grounder)
    num_landmarks = len(landmark_graph.get_landmark_atom_indices())
    heuristic = search.BlindHeuristic.create(problem)

    num_fluent_atoms = len(problem.get_repositories().get_fluent_ground_atoms())

    # One atomic goal per fluent goal conjunct, keeping the literal exactly as the problem states
    # it (polarity included) rather than rebuilding it.
    #
    # Conjuncts already satisfied in the initial state are dropped. In blocksworld p10-medium, for
    # instance, only 77 of 79 conjuncts need any work at all; keeping the rest would fill the table
    # with zero-expansion runs that every configuration "solves" identically and that say nothing
    # about width.
    initial_state, _ = state_repository.get_or_create_initial_state()
    initial_atom_indices = set(initial_state.get_fluent_atoms())

    goal_literals = [
        literal
        for literal in problem.get_fluent_goal_literals()
        if (literal.get_atom().get_index() in initial_atom_indices) != literal.get_polarity()
    ]
    if opts["max_goals"] > 0:
        goal_literals = goal_literals[: opts["max_goals"]]

    for goal_index, literal in enumerate(goal_literals):
        goal_condition = problem.get_or_create_ground_conjunctive_condition([], [literal], [], [])
        goal_atom = _atom_to_string(literal.get_atom())

        for config in configs:
            base = Row(
                domain=domain_name,
                split=split_name,
                problem=problem_name,
                goal_index=goal_index,
                goal_atom=goal_atom,
                config=config.name,
                status="",
                plan_length=0,
                num_expanded=0,
                num_generated=0,
                num_novelty_rejected=0,
                search_time_ms=0.0,
                wall_time_ms=0.0,
                num_landmarks=num_landmarks,
                num_fluent_atoms=num_fluent_atoms,
            )

            predicted = config.predicted_table_bytes(num_fluent_atoms, num_landmarks)
            if opts["max_table_bytes"] > 0 and predicted > opts["max_table_bytes"]:
                base.status = "SKIPPED_TABLE_TOO_LARGE"
                base.note = f"predicted dense table {predicted} bytes"
                rows.append(base)
                continue

            options = search.AStarIWOptions()
            options.width = config.width
            options.goal_strategy = search.ProblemGoalStrategy.create(problem, goal_condition)
            options.max_time_in_ms = opts["max_time_in_ms"]
            options.max_num_states = opts["max_num_states"]
            event_handler = search.DefaultAStarIWEventHandler(problem, True)
            options.event_handler = event_handler
            if config.landmarks:
                options.landmark_novelty_graph = landmark_graph

            started = time.perf_counter()
            try:
                result = search.find_solution_astar_iw(context, heuristic, options)
                status = str(result.status).split(".")[-1]
                plan_length = len(result.plan.get_actions()) if result.plan is not None else 0
            except Exception as exc:  # noqa: BLE001 - a failed config must not lose the whole problem
                status = "ERROR"
                plan_length = 0
                base.note = f"{type(exc).__name__}: {exc}"
            base.wall_time_ms = (time.perf_counter() - started) * 1000.0

            statistics = event_handler.get_statistics()
            base.status = status
            base.plan_length = plan_length
            base.num_expanded = statistics.get_num_expanded()
            base.num_generated = statistics.get_num_generated()
            base.num_novelty_rejected = statistics.get_num_novelty_rejected()
            base.search_time_ms = statistics.get_search_time_ms().total_seconds() * 1000.0
            rows.append(base)

    return [asdict(row) for row in rows]


def _worker_entry(domain_dir: str, problem_file: str, opts: dict, out_queue) -> None:
    try:
        out_queue.put(("ok", run_problem(domain_dir, problem_file, opts)))
    except Exception as exc:  # noqa: BLE001
        out_queue.put(("error", f"{Path(problem_file).name}: {type(exc).__name__}: {exc}"))


# ---------------------------------------------------------------------------------------------
# Scheduling
# ---------------------------------------------------------------------------------------------


class Task:
    """One problem, run in its own process so the parent can kill it if it overruns.

    A pool would be simpler, but neither a pool nor a signal-based timeout can interrupt a long
    call inside the native planner: Python signal handlers only run between bytecodes. Killing the
    process is the only reliable way to bound a task that hangs while grounding.
    """

    def __init__(self, domain_dir: Path, problem_file: Path, opts: dict, ctx):
        self.domain_dir = domain_dir
        self.problem_file = problem_file
        self.queue = ctx.Queue()
        self.process = ctx.Process(
            target=_worker_entry, args=(str(domain_dir), str(problem_file), opts, self.queue), daemon=True
        )
        self.deadline = time.monotonic() + opts["problem_timeout_s"]
        self.process.start()

    def poll(self):
        """Return (kind, payload) once finished, else None. Kills the process past its deadline."""
        try:
            message = self.queue.get_nowait()
            self.process.join(timeout=5)
            return message
        except queue.Empty:
            pass

        if not self.process.is_alive():
            self.process.join()
            return ("error", f"{self.problem_file.name}: worker died (exit {self.process.exitcode})")

        if time.monotonic() > self.deadline:
            self.process.terminate()
            self.process.join(timeout=5)
            if self.process.is_alive():
                self.process.kill()
                self.process.join()
            return ("timeout", f"{self.problem_file.name}: exceeded problem timeout")

        return None


def collect_problems(data_dir: Path, domains: list[str] | None, splits: list[str], limit: int):
    jobs = []
    for domain_dir in sorted(p for p in data_dir.iterdir() if p.is_dir()):
        if domains and domain_dir.name not in domains:
            continue
        for split in splits:
            split_dir = domain_dir / split
            if not split_dir.is_dir():
                continue
            if not (split_dir / "domain.pddl").is_file():
                continue
            problems = sorted(p for p in split_dir.glob("*.pddl") if p.name != "domain.pddl")
            if limit > 0:
                problems = problems[:limit]
            jobs.extend((split_dir, problem) for problem in problems)
    return jobs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data-dir", type=Path, default=DEFAULT_DATA_DIR)
    parser.add_argument("--domains", nargs="*", default=None, help="domain directory names; default all")
    parser.add_argument("--splits", nargs="*", default=["train"], help="subdirectories to take problems from")
    parser.add_argument("--limit", type=int, default=0, help="max problems per domain split; 0 = all")
    parser.add_argument("--max-goals", type=int, default=0, help="max atomic goals per problem; 0 = all")
    parser.add_argument("-j", "--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    parser.add_argument("--max-time-ms", type=int, default=30000, help="per-search budget")
    parser.add_argument("--max-num-states", type=int, default=2000000, help="per-search node cap")
    parser.add_argument(
        "--problem-timeout-s",
        type=float,
        default=1800.0,
        help="wall-clock budget for one problem across all its goals and configurations",
    )
    parser.add_argument(
        "--max-table-bytes",
        type=int,
        default=2 * 1024**3,
        help="skip a configuration whose dense novelty table would exceed this; 0 disables",
    )
    parser.add_argument("--include-iw3", action="store_true", help="also run IW(3) (see module docstring)")
    parser.add_argument("--out", type=Path, default=Path("liw_atomic_goals.jsonl"))
    args = parser.parse_args()

    opts = {
        "max_time_in_ms": args.max_time_ms,
        "max_num_states": args.max_num_states,
        "max_table_bytes": args.max_table_bytes,
        "max_goals": args.max_goals,
        "include_iw3": args.include_iw3,
        "problem_timeout_s": args.problem_timeout_s,
    }

    jobs = collect_problems(args.data_dir, args.domains, args.splits, args.limit)
    if not jobs:
        print(f"no problems found under {args.data_dir}", file=sys.stderr)
        return 1

    print(f"{len(jobs)} problems, {args.jobs} workers -> {args.out}", flush=True)

    # "spawn" keeps each worker's native planner state independent of the parent's.
    ctx = mp.get_context("spawn")

    started = time.monotonic()
    pending = list(jobs)
    running: list[Task] = []
    num_rows = 0
    num_done = 0
    num_failed = 0

    with args.out.open("w") as out_file:
        while pending or running:
            while pending and len(running) < args.jobs:
                domain_dir, problem_file = pending.pop(0)
                running.append(Task(domain_dir, problem_file, opts, ctx))

            progressed = False
            for task in list(running):
                message = task.poll()
                if message is None:
                    continue
                running.remove(task)
                progressed = True
                num_done += 1

                kind, payload = message
                if kind == "ok":
                    for row in payload:
                        out_file.write(json.dumps(row) + "\n")
                    num_rows += len(payload)
                    out_file.flush()
                else:
                    num_failed += 1
                    print(f"  [{kind}] {payload}", file=sys.stderr, flush=True)

                elapsed = time.monotonic() - started
                print(
                    f"  {num_done}/{len(jobs)} problems, {num_rows} rows, {num_failed} failed, {elapsed:.0f}s",
                    flush=True,
                )

            if not progressed:
                time.sleep(0.2)

    print(f"done in {time.monotonic() - started:.0f}s: {num_rows} rows -> {args.out}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
