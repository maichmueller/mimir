#!/usr/bin/env python3

import argparse
import multiprocessing as mp
import os
import pathlib
import subprocess
import sys
from dataclasses import dataclass


@dataclass(frozen=True)
class Task:
    label: str
    argv: list[str]
    log_path: pathlib.Path


def _run_task(task: Task) -> tuple[str, int, str, str]:
    result = subprocess.run(task.argv, capture_output=True, text=True)
    combined_output = result.stdout + result.stderr
    task.log_path.write_text(combined_output)
    return (task.label, result.returncode, str(task.log_path), combined_output)


def _parse_csv(value: str) -> list[str]:
    return [entry.strip() for entry in value.split(",") if entry.strip()]


def _build_tasks(
    root_dir: pathlib.Path,
    bench_bin: pathlib.Path,
    reps: int,
    ratio: float,
    output_dir: pathlib.Path,
    max_arity: int,
    beam_mode: str,
    threads: list[str],
    chunk_sizes: list[str],
    beam_width: int | None,
    bases: list[str],
    variants: list[str],
    include_grounded: bool,
    include_lifted: bool,
    grounded_domain: pathlib.Path,
    grounded_problem: pathlib.Path,
    lifted_domain: pathlib.Path,
    lifted_problem: pathlib.Path,
    projective_keep_depth_one_novel: bool,
    extra_args: list[str],
) -> list[Task]:
    domains: list[tuple[str, pathlib.Path, pathlib.Path, list[str]]] = []
    if include_grounded:
        domains.append(("grounded", grounded_domain, grounded_problem, []))
    if include_lifted:
        domains.append(("lifted", lifted_domain, lifted_problem, ["--mode", "lifted"]))

    tasks: list[Task] = []
    for domain_label, domain_file, problem_file, extra in domains:
        for basis in bases:
            for variant in variants:
                label = f"{domain_label}__{basis}__{variant}"
                argv = [
                    str(bench_bin),
                    str(domain_file),
                    str(problem_file),
                    str(max_arity),
                    str(reps),
                    beam_mode,
                    *threads,
                    "--iw1-basis",
                    basis,
                    "--iw1-action-selection",
                    variant,
                    "--iw1-atom-first-ratio",
                    str(ratio),
                    "--projective-keep-depth-one-novel",
                    "true" if projective_keep_depth_one_novel else "false",
                ]
                if beam_width is not None:
                    argv += ["--beam-width", str(beam_width)]
                if chunk_sizes:
                    argv += ["--chunk-sizes", *chunk_sizes]
                argv += extra
                argv += extra_args
                tasks.append(Task(label=label, argv=argv, log_path=output_dir / f"{label}.log"))
    return tasks


def main() -> int:
    parser = argparse.ArgumentParser(description="Run IW1 serial comparison benchmark variations in parallel processes.")
    parser.add_argument("--build-dir", default="build_codex", help="Build directory containing tests/unit/search_iw_parallel_benchmark.")
    parser.add_argument("--reps", type=int, default=5, help="Repetitions per variation.")
    parser.add_argument("--ratio", type=float, default=2.0, help="IW1 atom-first ratio.")
    parser.add_argument("--max-arity", type=int, default=1, help="IW max_arity passed to benchmark.")
    parser.add_argument("--beam-mode", choices=["all_tested", "survivors_only"], default="all_tested", help="Beam novelty mode.")
    parser.add_argument("--threads", default="1", help="Comma-separated thread counts passed to benchmark (e.g. 1 or 1,2,4).")
    parser.add_argument("--chunk-sizes", default="", help="Comma-separated chunk sizes. Empty means benchmark default.")
    parser.add_argument(
        "--beam-width",
        type=int,
        default=None,
        help="Optional beam width. Omit for plain IW1 (beam off).",
    )
    parser.add_argument(
        "--bases",
        default="classical,projective",
        help="Comma-separated novelty bases from {classical,projective,projective_typed}.",
    )
    parser.add_argument(
        "--projective-keep-depth-one-novel",
        choices=["true", "false"],
        default="true",
        help="Whether projective IW1 keeps all depth-1 children from the root as novel.",
    )
    parser.add_argument(
        "--variants",
        default="off,action_first,atom_first",
        help="Comma-separated action-selection variants from {off,action_first,atom_first}.",
    )
    parser.add_argument("--include-grounded", action="store_true", help="Include grounded task set.")
    parser.add_argument("--include-lifted", action="store_true", help="Include lifted task set.")
    parser.add_argument(
        "--grounded-domain",
        default="data/schedule/domain.pddl",
        help="Grounded domain path relative to repo root.",
    )
    parser.add_argument(
        "--grounded-problem",
        default="data/schedule/test_problem.pddl",
        help="Grounded problem path relative to repo root.",
    )
    parser.add_argument(
        "--lifted-domain",
        default="data/delivery/domain.pddl",
        help="Lifted domain path relative to repo root.",
    )
    parser.add_argument(
        "--lifted-problem",
        default="data/delivery/test_problem.pddl",
        help="Lifted problem path relative to repo root.",
    )
    parser.add_argument(
        "--extra-arg",
        action="append",
        default=[],
        help="Additional argument passed through to every benchmark task. Repeat for multiple args.",
    )
    parser.add_argument(
        "--workers",
        type=int,
        default=max(1, min(8, os.cpu_count() or 1)),
        help="Number of parallel worker processes.",
    )
    parser.add_argument(
        "--output-dir",
        default=None,
        help="Directory for per-variation logs. Defaults to tests/benchmark_logs/iw1_parallel_<pid>.",
    )
    args = parser.parse_args()

    root_dir = pathlib.Path(__file__).resolve().parents[1]
    bench_bin = root_dir / args.build_dir / "tests" / "unit" / "search_iw_parallel_benchmark"
    if not bench_bin.exists():
        print(f"Benchmark binary not found: {bench_bin}", file=sys.stderr)
        print(f"Build it first, e.g. cmake --build {root_dir / args.build_dir} --target search_iw_parallel_benchmark", file=sys.stderr)
        return 1

    if args.reps <= 0:
        print("--reps must be positive.", file=sys.stderr)
        return 1
    if args.ratio <= 0.0:
        print("--ratio must be positive.", file=sys.stderr)
        return 1
    if args.workers <= 0:
        print("--workers must be positive.", file=sys.stderr)
        return 1

    bases = _parse_csv(args.bases)
    variants = _parse_csv(args.variants)
    threads = _parse_csv(args.threads)
    chunk_sizes = _parse_csv(args.chunk_sizes) if args.chunk_sizes else []

    valid_bases = {"classical", "projective", "projective_typed"}
    valid_variants = {"off", "action_first", "atom_first"}
    if not bases or any(base not in valid_bases for base in bases):
        print(f"--bases must be a non-empty subset of {sorted(valid_bases)}.", file=sys.stderr)
        return 1
    if not variants or any(variant not in valid_variants for variant in variants):
        print(f"--variants must be a non-empty subset of {sorted(valid_variants)}.", file=sys.stderr)
        return 1
    if not threads:
        print("--threads must provide at least one value.", file=sys.stderr)
        return 1
    if args.max_arity <= 0:
        print("--max-arity must be positive.", file=sys.stderr)
        return 1
    if args.beam_width is not None and args.beam_width <= 0:
        print("--beam-width must be positive when provided.", file=sys.stderr)
        return 1

    include_grounded = args.include_grounded
    include_lifted = args.include_lifted
    if not include_grounded and not include_lifted:
        include_grounded = True
        include_lifted = True

    grounded_domain = (root_dir / args.grounded_domain).resolve()
    grounded_problem = (root_dir / args.grounded_problem).resolve()
    lifted_domain = (root_dir / args.lifted_domain).resolve()
    lifted_problem = (root_dir / args.lifted_problem).resolve()
    if include_grounded and (not grounded_domain.exists() or not grounded_problem.exists()):
        print(f"Grounded files missing: {grounded_domain} / {grounded_problem}", file=sys.stderr)
        return 1
    if include_lifted and (not lifted_domain.exists() or not lifted_problem.exists()):
        print(f"Lifted files missing: {lifted_domain} / {lifted_problem}", file=sys.stderr)
        return 1

    output_dir = pathlib.Path(args.output_dir) if args.output_dir else (root_dir / "tests" / "benchmark_logs" / f"iw1_parallel_{os.getpid()}")
    output_dir.mkdir(parents=True, exist_ok=True)

    tasks = _build_tasks(
        root_dir=root_dir,
        bench_bin=bench_bin,
        reps=args.reps,
        ratio=args.ratio,
        output_dir=output_dir,
        max_arity=args.max_arity,
        beam_mode=args.beam_mode,
        threads=threads,
        chunk_sizes=chunk_sizes,
        beam_width=args.beam_width,
        bases=bases,
        variants=variants,
        include_grounded=include_grounded,
        include_lifted=include_lifted,
        grounded_domain=grounded_domain,
        grounded_problem=grounded_problem,
        lifted_domain=lifted_domain,
        lifted_problem=lifted_problem,
        projective_keep_depth_one_novel=(args.projective_keep_depth_one_novel == "true"),
        extra_args=args.extra_arg,
    )
    print(f"Running {len(tasks)} tasks with {args.workers} workers")
    print(f"Logs: {output_dir}")
    print("Scheduled tasks:")
    for index, task in enumerate(tasks, start=1):
        print(f"  [{index}/{len(tasks)}] {task.label}")

    failed: list[tuple[str, int, str]] = []
    completed = 0
    with mp.Pool(processes=args.workers) as pool:
        for label, returncode, log_path, output in pool.imap_unordered(_run_task, tasks):
            completed += 1
            status = "ok" if returncode == 0 else "failed"
            print(f"[{completed}/{len(tasks)}][{status}] {label} -> {log_path}")
            print(f"===== begin output: {label} =====")
            if output.strip():
                print(output, end="" if output.endswith("\n") else "\n")
            else:
                print("[no output]")
            print(f"===== end output: {label} =====")
            if returncode != 0:
                failed.append((label, returncode, log_path))

    if failed:
        print("\nSome tasks failed:", file=sys.stderr)
        for label, returncode, log_path in failed:
            print(f"  {label}: exit={returncode} log={log_path}", file=sys.stderr)
        return 2

    print("\nAll tasks completed successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
