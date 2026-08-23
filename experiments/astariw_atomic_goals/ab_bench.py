"""A/B driver: run every unit under the baseline core and the patched core back to back.

desk-01 is shared and its load drifts over hours, so running one whole suite and then
the other would let that drift land entirely on one arm. Here the two builds are
measured next to each other on the same goal, in alternating order, so drift hits both
equally and each unit yields a paired measurement.

Each measurement is still its own throwaway process, exactly as in the original suites.
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


def select_goals(conjuncts, cap):
    indexed = list(enumerate(conjuncts))
    if cap is None or len(indexed) <= cap:
        return indexed
    step = len(indexed) / cap
    return [indexed[int(i * step)] for i in range(cap)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-root", required=True)
    ap.add_argument("--domains", nargs="+", default=["blocksworld", "childsnack", "spanner", "transport"])
    ap.add_argument("--problems", nargs="+", default=["p01-medium", "p02-medium", "p03-medium", "p04-medium", "p05-medium"])
    ap.add_argument("--split", default="test")
    ap.add_argument("--widths", nargs="+", type=int, default=[1, 2])
    ap.add_argument("--modes", nargs="+", default=["iw", "astar_iw"])
    ap.add_argument("--heuristic", default="blind")
    ap.add_argument("--context", default="grounded")
    ap.add_argument("--max-goals", type=int, default=None)
    ap.add_argument("--max-time-ms", type=int, default=15_000)
    ap.add_argument("--max-states", type=int, default=1_000_000)
    ap.add_argument("--out", required=True)
    ap.add_argument("--workdir", default="/tmp/astariw_goals")
    ap.add_argument("--python-base", required=True)
    ap.add_argument("--python-opt", required=True)
    args = ap.parse_args()

    data_root = Path(args.data_root)
    workdir = Path(args.workdir)
    workdir.mkdir(parents=True, exist_ok=True)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    units = []
    for domain in args.domains:
        ddir = data_root / f"{domain}-ipc" / args.split
        domain_file = ddir / "domain.pddl"
        if not domain_file.exists():
            continue
        for pname in args.problems:
            pfile = ddir / f"{pname}.pddl"
            if not pfile.exists():
                continue
            src = pfile.read_text()
            conjuncts = goal_conjuncts(src)
            for gi, conj in select_goals(conjuncts, args.max_goals):
                gfile = workdir / f"{domain}__{pname}__g{gi:03d}.pddl"
                gfile.write_text(with_single_goal(src, conj))
                for width in args.widths:
                    for mode in args.modes:
                        units.append(dict(domain=domain, problem=pname, goal_index=gi, goal_atom=conj,
                                          domain_file=str(domain_file), goal_file=str(gfile),
                                          width=width, mode=mode))

    print(f"[ab] {len(units)} units x 2 builds -> {out}", flush=True)
    hard_timeout_s = args.max_time_ms / 1000.0 + 180.0

    def run(python, u):
        cmd = [python, str(HERE / "run_one.py"),
               "--domain", u["domain_file"], "--problem", u["goal_file"],
               "--mode", u["mode"], "--width", str(u["width"]),
               "--max-time-ms", str(args.max_time_ms), "--max-states", str(args.max_states),
               "--heuristic", args.heuristic, "--context", args.context]
        t0 = time.perf_counter()
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=hard_timeout_s)
        except subprocess.TimeoutExpired:
            return {"status": "HARNESS_TIMEOUT", "harness_wall_s": time.perf_counter() - t0}
        if proc.returncode == 0 and proc.stdout.strip():
            rec = json.loads(proc.stdout.strip().splitlines()[-1])
            rec["harness_wall_s"] = time.perf_counter() - t0
            return rec
        return {"status": "WORKER_ERROR", "returncode": proc.returncode,
                "stderr": proc.stderr[-1500:], "harness_wall_s": time.perf_counter() - t0}

    done = 0
    t_start = time.perf_counter()
    with out.open("w") as fh:
        for i, u in enumerate(units):
            # Alternate which build goes first so neither systematically benefits from
            # a warm page cache on the problem file.
            order = [("base", args.python_base), ("opt", args.python_opt)]
            if i % 2:
                order.reverse()
            results = {tag: run(py, u) for tag, py in order}
            rec = {k: u[k] for k in ("domain", "problem", "goal_index", "goal_atom", "width", "mode")}
            rec["heuristic"] = args.heuristic
            rec["context"] = args.context
            rec["base"] = results["base"]
            rec["opt"] = results["opt"]
            fh.write(json.dumps(rec) + "\n")
            fh.flush()
            os.fsync(fh.fileno())
            done += 1
            if done % 10 == 0 or done == len(units):
                rate = done / (time.perf_counter() - t_start)
                print(f"[ab] {done}/{len(units)}  {rate*60:.1f} units/min  "
                      f"ETA {(len(units)-done)/rate/60:.1f} min", flush=True)
    print("[ab] done", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
