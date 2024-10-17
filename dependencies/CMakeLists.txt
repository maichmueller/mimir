cmake_minimum_required(VERSION 3.21)
project(InstallNauty)

include(ExternalProject)

message(STATUS "Dependency \"nauty\"...")

find_package(Nauty QUIET PATHS ${CMAKE_INSTALL_PREFIX} ${CMAKE_INSTALL_PREFIX}/include ${CMAKE_INSTALL_PREFIX}/lib NO_DEFAULT_PATH)

if (NOT Nauty_FOUND)

    message(STATUS "Preparing external project \"nauty\".")

    # We need to build nauty for cmake support
    ExternalProject_Add(
            nauty
            URL https://users.cecs.anu.edu.au/~bdm/nauty/nauty2_8_8.tar.gz
            PREFIX ${CMAKE_BINARY_DIR}/nauty
            CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> ./configure --prefix=<INSTALL_DIR> CFLAGS=-fPIC --enable-tls
            BUILD_COMMAND ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> make
            INSTALL_COMMAND ${CMAKE_COMMAND} -E chdir <SOURCE_DIR> make install
            INSTALL_DIR ${CMAKE_INSTALL_PREFIX}
    )
else ()
    message(STATUS "nauty is already installed.")
endif ()


cmake_minimum_required(VERSION 3.21)
project(InstallLoki)

include(ExternalProject)

message(STATUS "Dependency \"loki\"...")


## Attempt to find the installed loki library
#find_package(loki QUIET PATHS ${CMAKE_INSTALL_PREFIX} NO_DEFAULT_PATH)
#
#if (NOT loki_FOUND)
#    list(APPEND CMAKE_ARGS
#            -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
#            -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
#            -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
#    )
#
#    message(STATUS "Preparing external project \"loki\" with args:")
#    foreach(CMAKE_ARG ${CMAKE_ARGS})
#        message(STATUS "-- ${CMAKE_ARG}")
#    endforeach()
#    ExternalProject_Add(
#            loki
#            GIT_REPOSITORY https://github.com/drexlerd/Loki.git
#            GIT_TAG v0.0.6
#            PREFIX ${CMAKE_BINARY_DIR}/loki
#            DEPENDS boost
#            CMAKE_ARGS ${CMAKE_ARGS}
#    )
#else()
#    message(STATUS "loki is already installed.")
#endif()

