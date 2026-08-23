#!/usr/bin/env python3
"""Summarize the JSONL produced by `liw_atomic_goals.py`.

Reports coverage, head-to-head wins, and cost on jointly solved goals. The last one matters most:
comparing mean expansions over *all* goals mixes in the ones a configuration never solved, where a
timeout looks arbitrarily cheap or expensive depending on the cap, so cost is only ever compared on
goals that both configurations solved.

Usage:
    python experiments/liw_atomic_goals_report.py results.jsonl
    python experiments/liw_atomic_goals_report.py results.jsonl --per-domain
"""

from __future__ import annotations

import argparse
import json
import statistics
from collections import defaultdict
from pathlib import Path

CONFIG_ORDER = ["IW(1)", "LIW(1)", "IW(2)", "LIW(2)", "IW(3)"]


def load(path: Path):
    rows = [json.loads(line) for line in path.open() if line.strip()]
    by_goal = defaultdict(dict)
    for row in rows:
        key = (row["domain"], row["split"], row["problem"], row["goal_index"])
        by_goal[key][row["config"]] = row
    return rows, by_goal


def configs_present(rows) -> list[str]:
    seen = {row["config"] for row in rows}
    return [config for config in CONFIG_ORDER if config in seen] + sorted(seen - set(CONFIG_ORDER))


def fmt_int(value) -> str:
    return f"{value:,}" if value is not None else "-"


def coverage_table(rows, configs, group_key=None) -> None:
    groups = defaultdict(lambda: defaultdict(lambda: [0, 0, 0]))  # group -> config -> [solved, skipped, total]
    for row in rows:
        group = row[group_key] if group_key else "all"
        entry = groups[group][row["config"]]
        entry[2] += 1
        if row["status"] == "SOLVED":
            entry[0] += 1
        elif row["status"].startswith("SKIPPED"):
            entry[1] += 1

    header = f"{'group':<20}" + "".join(f"{config:>16}" for config in configs)
    print(header)
    print("-" * len(header))
    for group in sorted(groups):
        cells = []
        for config in configs:
            solved, skipped, total = groups[group][config]
            suffix = f" (+{skipped}sk)" if skipped else ""
            cells.append(f"{solved}/{total}{suffix}")
        print(f"{group:<20}" + "".join(f"{cell:>16}" for cell in cells))
    print()


def head_to_head(by_goal, left: str, right: str) -> None:
    left_only = right_only = both = neither = 0
    for entry in by_goal.values():
        if left not in entry or right not in entry:
            continue
        left_solved = entry[left]["status"] == "SOLVED"
        right_solved = entry[right]["status"] == "SOLVED"
        if left_solved and right_solved:
            both += 1
        elif left_solved:
            left_only += 1
        elif right_solved:
            right_only += 1
        else:
            neither += 1
    total = left_only + right_only + both + neither
    if total == 0:
        return
    print(f"{left} vs {right}: both {both}, only {left} {left_only}, only {right} {right_only}, neither {neither}")


def cost_on_jointly_solved(by_goal, configs) -> None:
    """Expansions and wall time on goals every listed configuration solved."""
    samples = defaultdict(lambda: ([], []))
    num_goals = 0
    for entry in by_goal.values():
        if any(config not in entry or entry[config]["status"] != "SOLVED" for config in configs):
            continue
        num_goals += 1
        for config in configs:
            expansions, times = samples[config]
            expansions.append(entry[config]["num_expanded"])
            times.append(entry[config]["wall_time_ms"])

    if num_goals == 0:
        print("no goal was solved by every configuration\n")
        return

    print(f"cost on the {num_goals} goals solved by every configuration")
    header = f"{'config':<10}{'med exp':>12}{'mean exp':>12}{'total exp':>14}{'med ms':>10}{'total ms':>12}"
    print(header)
    print("-" * len(header))
    for config in configs:
        expansions, times = samples[config]
        print(
            f"{config:<10}{fmt_int(int(statistics.median(expansions))):>12}"
            f"{fmt_int(int(statistics.mean(expansions))):>12}{fmt_int(sum(expansions)):>14}"
            f"{statistics.median(times):>10.1f}{sum(times):>12.1f}"
        )
    print()


def plan_quality(by_goal) -> None:
    """Whether LIW returns the same plan lengths as the IW rung it is meant to stand in for."""
    for liw, iw in [("LIW(1)", "IW(2)"), ("LIW(2)", "IW(3)")]:
        shorter = longer = equal = 0
        for entry in by_goal.values():
            if liw not in entry or iw not in entry:
                continue
            if entry[liw]["status"] != "SOLVED" or entry[iw]["status"] != "SOLVED":
                continue
            left, right = entry[liw]["plan_length"], entry[iw]["plan_length"]
            if left < right:
                shorter += 1
            elif left > right:
                longer += 1
            else:
                equal += 1
        if shorter or longer or equal:
            print(f"plan length {liw} vs {iw}: equal {equal}, {liw} shorter {shorter}, {liw} longer {longer}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("results", type=Path)
    parser.add_argument("--per-domain", action="store_true")
    args = parser.parse_args()

    rows, by_goal = load(args.results)
    configs = configs_present(rows)

    print(f"{len(rows)} runs over {len(by_goal)} atomic goals, configurations: {', '.join(configs)}\n")

    print("coverage (solved / attempted)")
    coverage_table(rows, configs)
    if args.per_domain:
        print("coverage by domain")
        coverage_table(rows, configs, group_key="domain")

    print("head-to-head")
    for left, right in [("LIW(1)", "IW(1)"), ("LIW(1)", "IW(2)"), ("LIW(2)", "IW(2)"), ("LIW(2)", "IW(3)")]:
        if left in configs and right in configs:
            head_to_head(by_goal, left, right)
    print()

    cost_on_jointly_solved(by_goal, [config for config in configs if config != "IW(3)"])
    plan_quality(by_goal)

    statuses = defaultdict(lambda: defaultdict(int))
    for row in rows:
        statuses[row["config"]][row["status"]] += 1
    print("\nstatus breakdown")
    for config in configs:
        parts = ", ".join(f"{status} {count}" for status, count in sorted(statuses[config].items()))
        print(f"  {config:<8} {parts}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
