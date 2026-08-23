cmake_minimum_required(VERSION 3.21)
project(InstallLoki)

include(ExternalProject)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/configure_loki.cmake)

message(STATUS "Dependency \"loki\"...")

# `loki::IndexedHashSet` refuses to chain more than one level deep, and resolves an index by
# reaching straight into `m_parent->m_vector` instead of recursing through the parent's own
# partitioning. Both are correct for the one chain upstream builds (domain -> problem) and wrong
# for the one Mimir's lifted grounding overlays need (domain -> problem -> overlay), where each
# search worker gets a private child of the shared problem repositories.
#
# The patch removes the depth guard and routes every parent access through the parent's public
# recursive API. Layers stay disjoint and indices still continue the parent's space, so
# single-level behaviour is unchanged. See docs/ROLLOUT_IW_IMPLEMENTATION_PLAN.md (T1).
#
# Default ON: `ProblemImpl::create_grounding_overlay` throws without it, and the lifted
# atomic-goal portfolio is built on top of that. Turn it off only to get stock loki behaviour.
option(MIMIR_PATCH_LOKI_MULTILEVEL_CHAINING
       "Patch loki's IndexedHashSet to support multi-level parent chaining (see comment above)" ON)

if(MIMIR_PATCH_LOKI_MULTILEVEL_CHAINING)
    find_package(Git REQUIRED)

    # `git checkout -- .` first, so the step is idempotent: re-running restores the pinned commit
    # before applying, instead of failing on an already-patched tree.
    set(LOKI_PATCH_COMMAND
        ${GIT_EXECUTABLE} -C <SOURCE_DIR> checkout -- .
        COMMAND ${GIT_EXECUTABLE} -C <SOURCE_DIR> apply ${MIMIR_LOKI_MULTILEVEL_PATCH})
else()
    set(LOKI_PATCH_COMMAND "")
endif()

# Attempt to find the installed loki library
find_package(loki QUIET PATHS ${CMAKE_INSTALL_PREFIX} NO_DEFAULT_PATH)

if (NOT loki_FOUND)


    list(APPEND CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    )

    message(STATUS "Preparing external project \"loki\" with args:")
    foreach(CMAKE_ARG ${CMAKE_ARGS})
        message(STATUS "-- ${CMAKE_ARG}")
    endforeach()
    if (MIMIR_PATCH_LOKI_MULTILEVEL_CHAINING)
        message(STATUS "-- applying local loki patch: IndexedHashSet multi-level chaining")
    endif()
    ExternalProject_Add(
            loki
            GIT_REPOSITORY https://github.com/drexlerd/Loki.git
            GIT_TAG 02880ea1d4ce4ca11deca188050ea6cc83f88dbb
            PREFIX ${CMAKE_BINARY_DIR}/loki
            DEPENDS boost abseil fmt argparse
            PATCH_COMMAND ${LOKI_PATCH_COMMAND}
            CMAKE_ARGS ${CMAKE_ARGS}
    )
else()
    message(STATUS "loki is already installed.")

    # The ExternalProject above -- and therefore its patch step -- does not exist on this branch,
    # so bring the already-installed headers up to date directly. Mimir's own configure repeats
    # this check for the same reason; both are idempotent.
    if(MIMIR_PATCH_LOKI_MULTILEVEL_CHAINING)
        mimir_patch_loki_tree("${CMAKE_INSTALL_PREFIX}" "installed loki headers" _installed_patched)
        mimir_patch_loki_tree("${CMAKE_BINARY_DIR}/loki/src/loki" "loki source checkout" _source_patched)
    endif()
endif()
