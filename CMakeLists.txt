cmake_minimum_required(VERSION 3.21)
project(mimir VERSION 0.1.1)


##############################################################
# Language setup
##############################################################

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


##############################################################
# Options
##############################################################

# more options to the list of string, e.g., "Dense" "Sparse"
#set(STATE_REPR "Dense" CACHE STRING "Select the state representation type")
#set_property(CACHE STATE_REPR PROPERTY STRINGS "Dense")
#message("Use state representation compile flag: ${STATE_REPR}")


##############################################################
# Build Targets
##############################################################

option(BUILD_PYMIMIR "Build" OFF)
option(BUILD_TESTS "Build" OFF)
option(BUILD_PROFILING "Build" OFF)


##############################################################
# Common Settings
##############################################################

# make cache variables for install destinations
include(GNUInstallDirs)

if (MSVC)
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
    endif ()

    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W1 /EHsc /bigobj /MP")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " /IGNORE:4006,4044,4075")
else ()
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
    endif ()
    # TODO: Add -Wextra and fix all warnings
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto=auto")
    endif ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall -DNDEBUG")
    set(CMAKE_CXX_FLAGS_PROFILING "-O3 -Wall -pg")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -Wall -g3 -ggdb")
endif ()

message(STATUS "Build configuration: ${CMAKE_BUILD_TYPE}")

set(DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data/")
message("DATA_DIR: ${DATA_DIR}")

##############################################################
# CMake modules and macro files
##############################################################

list(APPEND CMAKE_MODULE_PATH
        "${PROJECT_SOURCE_DIR}/cmake"
)
include("configure_boost")
include("configure_ccache")
include("configure_loki")

##############################################################
# CCache
##############################################################

# CCache
configure_ccache()


##############################################################
# Dependency Handling
##############################################################

# set(CMAKE_FIND_DEBUG_MODE TRUE)

# Boost
# Find Boost headers only according to https://cmake.org/cmake/help/latest/module/FindBoost.html
configure_boost()
find_package(Boost ${BOOST_MIN_VERSION} REQUIRED COMPONENTS iostreams)
if (Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    message(STATUS "Found Boost: ${Boost_DIR} (found version ${Boost_VERSION})")
endif ()
include(ExternalProject)

# this is where we expect to find the nauty library if installed already
list(APPEND CMAKE_PREFIX_PATH
        ${CMAKE_BINARY_DIR}/deps/install
)
find_package(Nauty)
if (NOT Nauty_FOUND)
    message(STATUS "Nauty not found. Downloading and configuring from source.")
    configure_file(
            ${PROJECT_SOURCE_DIR}/dependencies/nauty/CMakeLists.txt
            ${CMAKE_BINARY_DIR}/deps/nauty/CMakeLists.txt
            COPYONLY
    )
    execute_process(
            COMMAND ${CMAKE_COMMAND}
            -S ${CMAKE_BINARY_DIR}/deps/nauty
            -B ${CMAKE_BINARY_DIR}/deps/build/nauty
            -G ${CMAKE_GENERATOR}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/deps/install
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    )
    message(STATUS "Building Nauty.")
    execute_process(
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/deps/build/nauty --parallel
    )
    message(STATUS "Installing Nauty.")
    execute_process(
            COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}/deps/build/nauty --config Release
    )
    find_package(Nauty REQUIRED)
endif ()

message(STATUS "Found Nauty: ${NAUTY_LIBRARY} ${NAUTY_INCLUDE_DIR}")

find_package(ZLIB REQUIRED)

find_package(loki ${LOKI_MIN_VERSION} QUIET COMPONENTS parsers)
if (NOT loki_FOUND)
    configure_file(
            ${PROJECT_SOURCE_DIR}/dependencies/loki/CMakeLists.txt
            ${CMAKE_BINARY_DIR}/deps/loki/CMakeLists.txt
            COPYONLY
    )
    configure_file(
            ${PROJECT_SOURCE_DIR}/dependencies/loki/conanfile.py
            ${CMAKE_BINARY_DIR}/deps/loki/conanfile.py
            COPYONLY
    )
    message(STATUS "Loki not found. Downloading and configuring from source.")
    execute_process(
            COMMAND
            ${CMAKE_COMMAND}
            -S ${CMAKE_BINARY_DIR}/deps/loki
            -B ${CMAKE_BINARY_DIR}/deps/build/loki
            -G ${CMAKE_GENERATOR}
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/deps/install
            -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=${PROJECT_SOURCE_DIR}/conan_provider.cmake
            -DCONAN_PROVIDER_FILE=${PROJECT_SOURCE_DIR}/conan_provider.cmake
            -DCONAN_COMMAND=${CONAN_COMMAND}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    )
    message(STATUS "Building Loki.")
    execute_process(
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/deps/build/loki --parallel
    )
    message(STATUS "Installing Loki.")
    execute_process(
            COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}/deps/build/loki --config Release
    )
    find_package(loki ${LOKI_MIN_VERSION} REQUIRED COMPONENTS parsers)
endif ()
message(STATUS "Found Loki: ${loki_DIR} (found version ${loki_VERSION})")

find_package(fmt REQUIRED)
message(STATUS "Found fmt: ${fmt_DIR} (found version ${fmt_VERSION})")

find_package(range-v3 REQUIRED)
message(STATUS "Found range-v3: ${range-v3_DIR} (found version ${range-v3_VERSION})")


##############################################################
# Add library and executable targets
##############################################################

# ------------
# Target Mimir
# ------------
add_subdirectory(src)

# -------------------
# Target Python Mimir
# -------------------
if (BUILD_PYMIMIR)
    add_subdirectory(python/src)
endif ()

# ----------
# Target Exe
# ----------
add_subdirectory(exe)

# ----------------
# Target Profiling
# ----------------
if (BUILD_PROFILING)
    add_subdirectory(benchmark)
endif ()

# -----------
# Target Test
# -----------
if (BUILD_TESTS)
    add_subdirectory(tests)
endif ()


###########
# Install #
###########

# Install header files
install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/mimir" "${PROJECT_SOURCE_DIR}/include/cista"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

# Install cmake scripts
install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/cmake/"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mimir/cmake"
)


###########
# Exports #
###########

# https://cmake.org/cmake/help/latest/guide/importing-exporting/index.html

include(CMakePackageConfigHelpers)

# Generate the version file for the config file
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/mimirConfigVersion.cmake"
        VERSION ${mimir_VERSION}
        COMPATIBILITY ExactVersion
)

# Create config file
# https://cmake.org/cmake/help/book/mastering-cmake/cmake/Help/guide/importing-exporting/index.html
# https://cmake.org/cmake/help/latest/module/CMakePackageConfigHelpers.html#generating-a-package-configuration-file
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/mimirConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mimir"
        NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

# Install config files
install(
        FILES
        "${CMAKE_CURRENT_BINARY_DIR}/mimirConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/mimirConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mimir"
)
