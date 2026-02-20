cmake_minimum_required(VERSION 3.21)
project(InstallMimir)

include(ExternalProject)


list(APPEND CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
    -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
    -DCMAKE_BUILD_TYPE=Release
    )

message(STATUS "Preparing external project \"mimir\" with args:")
foreach(CMAKE_ARG ${CMAKE_ARGS})
    message(STATUS "-- ${CMAKE_ARG}")
endforeach()

# Some dependencies may already be installed via cache and then do not create
# ExternalProject targets in this configure pass. Only depend on targets that exist.
set(MIMIR_DEPENDS)
foreach(dep IN ITEMS loki boost gtl fmt nauty abseil argparse oneTBB valla)
    if(TARGET ${dep})
        list(APPEND MIMIR_DEPENDS ${dep})
    endif()
endforeach()

ExternalProject_Add(
    mimir
    GIT_REPOSITORY https://github.com/simon-stahlberg/mimir.git
    GIT_TAG main
    PREFIX ${CMAKE_BINARY_DIR}/mimir
    DEPENDS ${MIMIR_DEPENDS}
    CMAKE_ARGS ${CMAKE_ARGS}
)
