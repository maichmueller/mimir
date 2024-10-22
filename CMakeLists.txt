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
# Dependency Handling
##############################################################

# set(CMAKE_FIND_DEBUG_MODE TRUE)

# Find Boost headers only according to https://cmake.org/cmake/help/latest/module/FindBoost.html
include("${PROJECT_SOURCE_DIR}/cmake/configure_boost.cmake")
configure_boost()
find_package(Boost ${BOOST_MIN_VERSION} REQUIRED COMPONENTS iostreams)
message(STATUS "Found Boost: ${Boost_DIR} (found version ${Boost_VERSION})")

find_package(nauty REQUIRED)
message(STATUS "Found Nauty: ${NAUTY_LIBRARY} ${NAUTY_INCLUDE_DIR}")

find_package(ZLIB REQUIRED)

find_package(loki ${LOKI_MIN_VERSION} REQUIRED COMPONENTS parsers)
message(STATUS "Found Loki: ${loki_DIR} (found version ${loki_VERSION})")

find_package(fmt REQUIRED)
message(STATUS "Found fmt: ${fmt_DIR} (found version ${fmt_VERSION})")

find_package(range-v3 REQUIRED)
message(STATUS "Found range-v3: ${range-v3_DIR} (found version ${range-v3_VERSION})")

find_package(Threads REQUIRED)

# This prints a summary of found dependencies
include(FeatureSummary)
feature_summary(WHAT ALL)


##############################################################
# Add library and executable targets
##############################################################
message(STATUS "Adding targets from subdirectories.")
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


# -------
# Sandbox
# -------
add_subdirectory(sandbox)

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
