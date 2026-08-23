cmake_minimum_required(VERSION 3.21)
project(InstallValla)

include(ExternalProject)

message(STATUS "Dependency \"valla\"...")

# `valla::IndexedHashSet::insert` takes its stripe mutex *before* the uniqueness lookup, so
# every read hit writes to one of only 64 shared cache lines. That serializes readers badly
# when several threads intern into one table. This patch hoists the lookup into a
# double-checked fast path. It is safe: `m_uniqueness` is a gtl::parallel_flat_hash_set that
# locks its own submaps, and `m_slots` is a tbb::concurrent_vector with stable indices.
#
# Measured on logistics-6 p-23 (see docs/PARALLEL_IW_ROLLOUTS.md section 7.2):
#   serial IW(1)                        155.9 ms -> 154.7 ms  (0.8%, i.e. noise)
#   K=16 rollouts, 16 threads, shared    11040 ms -> 4412 ms  (2.5x, but still slower
#                                                              than running them serially)
#   K=16 rollouts, 16 threads, private     313 ms ->  313 ms  (unaffected)
#
# Default OFF: Mimir's own parallel rollout path gives each search private interning tables
# and so never contends on a shared one, and carrying a patch against a pinned upstream tag
# means a GIT_TAG bump breaks the build for a benefit nothing currently consumes. Turn it on
# if you intern into one shared table from several threads.
option(MIMIR_PATCH_VALLA_LOCKFREE_READS
       "Patch valla's IndexedHashSet to look up outside the stripe lock (see comment above)" OFF)

# Attempt to find the installed valla library
find_package(valla QUIET PATHS ${CMAKE_INSTALL_PREFIX} NO_DEFAULT_PATH)

if (NOT valla_FOUND)


    list(APPEND CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DTBB_TEST=OFF
    )

    message(STATUS "Preparing external project \"valla\" with args:")
    foreach(CMAKE_ARG ${CMAKE_ARGS})
        message(STATUS "-- ${CMAKE_ARG}")
    endforeach()
    if (MIMIR_PATCH_VALLA_LOCKFREE_READS)
        # `git checkout -- .` first, so the step is idempotent: re-running restores the
        # pinned commit before applying, instead of failing on an already-patched tree.
        set(VALLA_PATCH_COMMAND
            git -C <SOURCE_DIR> checkout -- .
            COMMAND git -C <SOURCE_DIR> apply ${CMAKE_CURRENT_SOURCE_DIR}/patches/0001-indexed-hash-set-lock-free-read-path.patch)
        message(STATUS "-- applying local valla patch: lock-free IndexedHashSet read path")
    else()
        set(VALLA_PATCH_COMMAND "")
    endif()

    ExternalProject_Add(
        valla
        GIT_REPOSITORY https://github.com/drexlerd/tree-compression.git
        GIT_TAG f487d3fcba00b88bba1e88d9e111747f53679da1
        PREFIX ${CMAKE_BINARY_DIR}/valla
        DEPENDS abseil fmt oneTBB
        PATCH_COMMAND ${VALLA_PATCH_COMMAND}
        CMAKE_ARGS ${CMAKE_ARGS}
    )
else()
    message(STATUS "valla is already installed.")
endif()