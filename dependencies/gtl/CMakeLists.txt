cmake_minimum_required(VERSION 3.21)
project(InstallGTL)

include(ExternalProject)

message(STATUS "Dependency \"gtl\"...")

# Attempt to find the installed gtl library
find_package(gtl QUIET PATHS ${CMAKE_INSTALL_PREFIX} NO_DEFAULT_PATH)

if (NOT gtl_FOUND)


    list(APPEND CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
            -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            # -DCMAKE_CXX_FLAGS="-fsanitize=address"
            # Disable extras that drag in the failing example
            -DGTL_BUILD_EXAMPLES=OFF
            -DGTL_BUILD_TESTS=OFF
            -DGTL_BUILD_BENCHMARKS=OFF
    )

    message(STATUS "Preparing external project \"gtl\" with args:")
    foreach(CMAKE_ARG ${CMAKE_ARGS})
        message(STATUS "-- ${CMAKE_ARG}")
    endforeach()
    # Clamp gtl's destructive-interference padding to <= 128 bytes so that the alignment it induces on
    # valla::IndexedHashSet -> formalism::ProblemImpl stays under nanobind's `alignof(T) < (1 << 8)`
    # limit. See patch_interference_size.cmake for the full rationale; the patch is idempotent and is a
    # no-op on platforms already reporting <= 128.
    ExternalProject_Add(
        gtl
        GIT_REPOSITORY https://github.com/greg7mdp/gtl.git
        GIT_TAG dc7a24fe4b7f2f94480d954da6562c7381ace41f
        PATCH_COMMAND ${CMAKE_COMMAND}
                      -DGTL_CONFIG_HPP=<SOURCE_DIR>/include/gtl/gtl_config.hpp
                      -P ${CMAKE_CURRENT_SOURCE_DIR}/patch_interference_size.cmake
        PREFIX ${CMAKE_BINARY_DIR}/gtl
        CMAKE_ARGS ${CMAKE_ARGS}
    )
else()
    message(STATUS "gtl is already installed.")
endif()