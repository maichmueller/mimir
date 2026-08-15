"""Driver: AStarIW vs. IW(k) on atomic (single-atom) goals.

For every selected problem file we split its conjunctive goal into N single-atom
goals and, for each (goal, mode, width) unit, spawn a fresh worker process.
The worker owns the whole problem representation and dies right after reporting,
so nothing built for one goal or one mode survives into the next measurement.

Results are appended to a JSONL file as they arrive, so a run can be inspected
(or resumed) while it is still going.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

from pddl_goals import goal_conjuncts, with_single_goal

HERE = Path(__file__).parent


def select_goals(conjuncts: list[str], cap: int | None) -> list[tuple[int, str]]:
    """All conjuncts, or an evenly spaced deterministic subsample of ``cap`` of them."""
    indexed = list(enumerate(conjuncts))
    if cap is None or len(indexed) <= cap:
        return indexed
    step = len(indexed) / cap
    return [indexed[int(i * step)] for i in range(cap)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-root", required=True)
    ap.add_argument("--domains", nargs="+", default=["blocksworld", "childsnack", "spanner", "transport"])
    ap.add_argument("--problems", nargs="+", default=["p01-medium", "p03-medium", "p05-medium"])
    ap.add_argument("--split", default="test")
    ap.add_argument("--widths", nargs="+", type=int, default=[1, 2])
    ap.add_argument("--modes", nargs="+", default=["iw", "astar_iw"])
    ap.add_argument("--heuristic", default="blind")
    ap.add_argument("--context", default="grounded", choices=["grounded", "lifted"])
    ap.add_argument("--max-goals", type=int, default=None, help="cap goals per problem (evenly sampled)")
    ap.add_argument("--max-time-ms", type=int, default=30_000)
    ap.add_argument("--max-states", type=int, default=2_000_000)
    ap.add_argument("--out", required=True)
    ap.add_argument("--workdir", default="/tmp/astariw_goals")
    ap.add_argument("--python", default=sys.executable)
    args = ap.parse_args()

    data_root = Path(args.data_root)
    workdir = Path(args.workdir)
    workdir.mkdir(parents=True, exist_ok=True)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    # Build the full unit list first so progress reporting is meaningful.
    units = []
    for domain in args.domains:
        ddir = data_root / f"{domain}-ipc" / args.split
        domain_file = ddir / "domain.pddl"
        if not domain_file.exists():
            print(f"!! missing {domain_file}", file=sys.stderr)
            continue
        for pname in args.problems:
            pfile = ddir / f"{pname}.pddl"
            if not pfile.exists():
                print(f"!! missing {pfile}", file=sys.stderr)
                continue
            src = pfile.read_text()
            conjuncts = goal_conjuncts(src)
            chosen = select_goals(conjuncts, args.max_goals)
            for gi, conj in chosen:
                gfile = workdir / f"{domain}__{pname}__g{gi:03d}.pddl"
                gfile.write_text(with_single_goal(src, conj))
                for width in args.widths:
                    for mode in args.modes:
                        units.append(
                            {
                                "domain": domain,
                                "problem": pname,
                                "goal_index": gi,
                                "goal_atom": conj,
                                "num_goal_atoms_total": len(conjuncts),
                                "domain_file": str(domain_file),
                                "goal_file": str(gfile),
                                "width": width,
                                "mode": mode,
                            }
                        )

    print(f"[bench] {len(units)} units queued -> {out}", flush=True)
    # Hard kill margin on top of the in-search time budget: parsing/grounding
    # happens before the search clock starts.
    hard_timeout_s = args.max_time_ms / 1000.0 + 180.0

    done = 0
    t_start = time.perf_counter()
    with out.open("w") as fh:
        for u in units:
            cmd = [
                args.python,
                str(HERE / "run_one.py"),
                "--domain", u["domain_file"],
                "--problem", u["goal_file"],
                "--mode", u["mode"],
                "--width", str(u["width"]),
                "--max-time-ms", str(args.max_time_ms),
                "--max-states", str(args.max_states),
                "--heuristic", args.heuristic,
                "--context", args.context,
            ]
            t0 = time.perf_counter()
            try:
                proc = subprocess.run(cmd, capture_output=True, text=True, timeout=hard_timeout_s)
                elapsed = time.perf_counter() - t0
                if proc.returncode == 0 and proc.stdout.strip():
                    rec = json.loads(proc.stdout.strip().splitlines()[-1])
                    rec["harness_wall_s"] = elapsed
                else:
                    rec = {
                        "status": "WORKER_ERROR",
                        "returncode": proc.returncode,
                        "stderr": proc.stderr[-2000:],
                        "harness_wall_s": elapsed,
                        "mode": u["mode"],
                        "width": u["width"],
                        "heuristic": args.heuristic,
                        "context": args.context,
                    }
            except subprocess.TimeoutExpired:
                rec = {
                    "status": "HARNESS_TIMEOUT",
                    "harness_wall_s": time.perf_counter() - t0,
                    "mode": u["mode"],
                    "width": u["width"],
                    "heuristic": args.heuristic,
                    "context": args.context,
                }
            rec.update(
                {
                    "domain": u["domain"],
                    "problem": u["problem"],
                    "goal_index": u["goal_index"],
                    "goal_atom": u["goal_atom"],
                    "num_goal_atoms_total": u["num_goal_atoms_total"],
                }
            )
            fh.write(json.dumps(rec) + "\n")
            fh.flush()
            os.fsync(fh.fileno())
            done += 1
            if done % 10 == 0 or done == len(units):
                rate = done / (time.perf_counter() - t_start)
                eta = (len(units) - done) / rate if rate > 0 else float("nan")
                print(
                    f"[bench] {done}/{len(units)}  {rate*60:.1f} units/min  ETA {eta/60:.1f} min",
                    flush=True,
                )
    print("[bench] done", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
