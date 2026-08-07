#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build_tsan}"

BRFS_BIN="${BUILD_DIR}/tests/unit/search_brfs_test"
IW_BIN="${BUILD_DIR}/tests/unit/search_iw_test"
IW_BENCH_BIN="${BUILD_DIR}/tests/unit/search_iw_parallel_benchmark"

if [[ ! -x "${BRFS_BIN}" || ! -x "${IW_BIN}" ]]; then
    echo "Expected TSAN test binaries under ${BUILD_DIR}/tests/unit" >&2
    echo "Configure a TSAN build first, for example with -DCMAKE_BUILD_TYPE=RelWithDebInfo and thread sanitizer flags." >&2
    exit 1
fi

"${BRFS_BIN}" --gtest_filter="*ParallelBeam*:*DuplicatePruningCovers*"
"${IW_BIN}" --gtest_filter="*ParallelBeam*"
"${BRFS_BIN}" --gtest_filter="*LiftedKPKC*:*LiftedSymmetryPruning*:*LiftedExhaustive*"
"${IW_BIN}" --gtest_filter="*LiftedKPKC*:*LiftedSymmetryPruning*"

if [[ -x "${IW_BENCH_BIN}" ]]; then
    "${IW_BENCH_BIN}" \
        "${ROOT_DIR}/data/schedule/domain.pddl" \
        "${ROOT_DIR}/data/schedule/test_problem.pddl" \
        5 \
        256 \
        2 \
        all_tested \
        1 \
        2 \
        4
    "${IW_BENCH_BIN}" \
        "${ROOT_DIR}/data/delivery/domain.pddl" \
        "${ROOT_DIR}/data/delivery/test_problem.pddl" \
        3 \
        64 \
        2 \
        all_tested \
        1 \
        2 \
        4 \
        --mode lifted
fi
