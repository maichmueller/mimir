cmake_minimum_required(VERSION 3.21)
set(VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/__version__")
file(READ "${VERSION_FILE}" version)

# Match major, minor, and patch from a version string like "0.1.0" or "0.1.0.dev"
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ ${version})

# Check if the matches were successful
if(CMAKE_MATCH_COUNT EQUAL 3)
    set(version_major "${CMAKE_MATCH_1}")
    set(version_minor "${CMAKE_MATCH_2}")
    set(version_patch "${CMAKE_MATCH_3}")
    message("Mimir Version: ${version_major}.${version_minor}.${version_patch}")
else()
    message(FATAL_ERROR "Failed to parse version from ${VERSION_FILE}")
endif()

# Use parsed version components in the project
project(mimir VERSION "${version_major}.${version_minor}.${version_patch}" LANGUAGES C CXX)


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

include(cmake/dependencies.cmake)

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
install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/mimir"
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
