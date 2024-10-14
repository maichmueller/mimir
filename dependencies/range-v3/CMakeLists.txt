cmake_minimum_required(VERSION 3.21)
project(InstallFMT)

include(ExternalProject)


message(STATUS "Dependency \"range-v3\"...")

# Attempt to find the installed googletest library
find_package(range-v3 QUIET PATHS ${CMAKE_INSTALL_PREFIX} NO_DEFAULT_PATH)

if (NOT range-v3_FOUND)
    list(APPEND CMAKE_ARGS
            -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
    )
    message(STATUS "Preparing external project \"range-v3\" with args:")
    foreach (CMAKE_ARG ${CMAKE_ARGS})
        message(STATUS "-- ${CMAKE_ARG}")
    endforeach ()

    ExternalProject_Add(
            range-v3
            GIT_REPOSITORY https://github.com/ericniebler/range-v3
            GIT_TAG 0.12.0
            PREFIX ${CMAKE_BINARY_DIR}/range-v3
            CMAKE_ARGS ${CMAKE_ARGS}
    )
else ()
    message(STATUS "range-v3 is already installed.")
endif ()

