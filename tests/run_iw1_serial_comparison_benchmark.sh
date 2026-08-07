#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build_codex}"
REPS="${2:-5}"
IW_BENCH_BIN="${BUILD_DIR}/tests/unit/search_iw_parallel_benchmark"

if [[ ! -x "${IW_BENCH_BIN}" ]]; then
    echo "Expected benchmark binary at ${IW_BENCH_BIN}" >&2
    echo "Build it first, for example: cmake --build ${BUILD_DIR} --target search_iw_parallel_benchmark" >&2
    exit 1
fi

# Grounded serial IW1: classical + projective, and off/action_first/atom_first variants.
"${IW_BENCH_BIN}" \
    "${ROOT_DIR}/data/schedule/domain.pddl" \
    "${ROOT_DIR}/data/schedule/test_problem.pddl" \
    1 \
    "${REPS}" \
    all_tested \
    1 \
    --iw1-basis both \
    --iw1-action-selection all \
    --iw1-atom-first-ratio 2.0

# Lifted serial IW1: same matrix.
"${IW_BENCH_BIN}" \
    "${ROOT_DIR}/data/delivery/domain.pddl" \
    "${ROOT_DIR}/data/delivery/test_problem.pddl" \
    1 \
    "${REPS}" \
    all_tested \
    1 \
    --mode lifted \
    --iw1-basis both \
    --iw1-action-selection all \
    --iw1-atom-first-ratio 2.0
