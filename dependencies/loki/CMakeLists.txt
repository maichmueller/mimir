cmake_minimum_required(VERSION 3.21)
project(InstallLoki)

include(ExternalProject)

message(STATUS "Dependency \"loki\"...")

# Attempt to find the installed loki library
find_package(loki QUIET)

if (NOT loki_FOUND)
    list(APPEND CMAKE_ARGS
            -G ${CMAKE_GENERATOR}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
            -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=${CONAN_PROVIDER_FILE}
            -DCONAN_COMMAND=${CONAN_COMMAND}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    )

    message(STATUS "Preparing external project \"loki\" with args:")
    foreach(CMAKE_ARG ${CMAKE_ARGS})
        message(STATUS "-- ${CMAKE_ARG}")
    endforeach()
    ExternalProject_Add(
            loki
            GIT_REPOSITORY https://github.com/drexlerd/Loki.git
            GIT_TAG v0.0.6
            PREFIX ${CMAKE_BINARY_DIR}/loki
            CMAKE_ARGS ${CMAKE_ARGS}
            PATCH_COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/conanfile.py <SOURCE_DIR>/conanfile.py
    )
else()
    message(STATUS "loki is already installed.")
endif()