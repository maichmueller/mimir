cmake_minimum_required(VERSION 3.21)
project(InstallNanobind)

include(ExternalProject)

message(STATUS "Dependency \"nanobind\"...")

# Attempt to find the installed library
find_package(Python 3.12
  REQUIRED COMPONENTS Interpreter Development.Module
  OPTIONAL_COMPONENTS Development.SABIModule)

# nanobind's version is ABI-critical, not merely a feature level, so unlike the
# other dependencies here it must be matched exactly rather than accepted as
# "some install is present".
#
# Every nanobind extension derives the key under which it looks up nanobind's
# shared type registry from NB_INTERNALS_VERSION -- the builtins-dict entry
# `__nb_internals_v<N>_<platform_abi>_<domain>__` -- and that counter is bumped
# whenever nanobind's internal structs change layout (2.7.0 -> 16, 2.9.2 -> 16,
# 2.12.0 -> 19, 2.13.0 -> 20, 2.15.0 -> 21). Two modules built against different
# generations create *separate* registries and can no longer cast each other's
# C++ types. Nothing reports this: the libraries link, the modules import, and
# the failure surfaces as a TypeError the first time an object crosses the
# boundary -- which for pymimir means the first downstream extension that tries
# to accept an `mm.State`.
#
# The unversioned `find_package` this replaced accepted whatever happened to sit
# in the install prefix, so anyone who had built mimir before stayed silently
# pinned to their old nanobind and the GIT_TAG below never ran.
set(MIMIR_NANOBIND_VERSION "2.15.0" CACHE STRING
    "Pinned nanobind version. ABI-critical -- see the comment above.")

find_package(nanobind ${MIMIR_NANOBIND_VERSION} EXACT QUIET
             PATHS ${CMAKE_INSTALL_PREFIX} NO_DEFAULT_PATH)

if (NOT nanobind_FOUND)
    # A leftover install is not overwritten cleanly: nanobind ships its whole
    # Python package, so files dropped between releases would survive and stay
    # on the include path. Replace the tree rather than install over it. The
    # `EXISTS` guard keeps this pinned to a real nanobind install even if
    # CMAKE_INSTALL_PREFIX is somehow unset.
    if (nanobind_CONSIDERED_VERSIONS AND
        EXISTS "${CMAKE_INSTALL_PREFIX}/nanobind/cmake/nanobind-config.cmake")
        message(STATUS
                "Replacing nanobind ${nanobind_CONSIDERED_VERSIONS} in "
                "${CMAKE_INSTALL_PREFIX} with pinned ${MIMIR_NANOBIND_VERSION}.")
        file(REMOVE_RECURSE "${CMAKE_INSTALL_PREFIX}/nanobind")
    endif ()

    list(APPEND CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
            -DNB_TEST=OFF
    )

    message(STATUS "Preparing external project \"nanobind\" with args:")
    foreach(CMAKE_ARG ${CMAKE_ARGS})
        message(STATUS "-- ${CMAKE_ARG}")
    endforeach()


    # If the library is not found, use ExternalProject_Add to download and build it
    ExternalProject_Add(
            nanobind
            GIT_REPOSITORY https://github.com/wjakob/nanobind.git
            GIT_TAG v${MIMIR_NANOBIND_VERSION}
            PREFIX ${CMAKE_BINARY_DIR}/nanobind
            CMAKE_ARGS ${CMAKE_ARGS}
    )
else()
    message(STATUS "nanobind ${MIMIR_NANOBIND_VERSION} is already installed.")
endif()
