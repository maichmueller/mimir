#!/usr/bin/env bash
#
# ThreadSanitizer run for the atomic-goal IW portfolio and the pieces it is built on.
#
# The portfolio runs K+1 searches side by side over shared, entirely unsynchronized structures: in
# grounded mode one applicable-action generator and one axiom evaluator; in lifted mode one parsed
# model whose repositories each worker's grounding overlay is parented on. Nothing there takes a
# lock, so isolation is a design property that only a race detector can actually check.
#
# Configure a TSAN build first, for example:
#   cmake -S . -B build_tsan_v2 -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON \
#         -DCMAKE_PREFIX_PATH="$PWD/dependencies/installs" \
#         -DCMAKE_CXX_FLAGS="-fno-lto -fsanitize=thread" \
#         -DCMAKE_EXE_LINKER_FLAGS="-fno-lto -fsanitize=thread"

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build_tsan_v2}"

PORTFOLIO_BIN="${BUILD_DIR}/tests/unit/search_atomic_goal_iw_portfolio_test"
ROLLOUT_BIN="${BUILD_DIR}/tests/unit/search_rollout_iw_test"
OVERLAY_BIN="${BUILD_DIR}/tests/unit/formalism_problem_grounding_overlay_test"
CHAIN_BIN="${BUILD_DIR}/tests/unit/formalism_indexed_hash_set_chaining_test"

if [[ ! -x "${PORTFOLIO_BIN}" ]]; then
    echo "Expected TSAN test binaries under ${BUILD_DIR}/tests/unit" >&2
    echo "See the header of this script for a configure line." >&2
    exit 1
fi

# The overlay and chaining tests are single-threaded, but they establish the invariants the parallel
# run depends on -- run them first so a failure there is not misread as a race.
"${CHAIN_BIN}"
"${OVERLAY_BIN}"
"${ROLLOUT_BIN}"

# The whole portfolio suite: grounded and lifted, serial and parallel, with and without axioms.
"${PORTFOLIO_BIN}"
