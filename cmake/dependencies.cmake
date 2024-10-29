# set(CMAKE_FIND_DEBUG_MODE TRUE)

# macos setting for losing warnings of the kind:
# ld: warning: search path '/Users/runner/.conan2/p/range0301bf3d76d5d/p/lib' not found
if(POLICY CMP0142)
    cmake_policy(SET CMP0142 NEW)
endif()


# Find Boost headers only according to https://cmake.org/cmake/help/latest/module/FindBoost.html
set(Boost_NO_SYSTEM_PATHS ON)
find_package(Boost 1.74.0 REQUIRED COMPONENTS iostreams)
message(STATUS "Found Boost: ${Boost_DIR} (found version ${Boost_VERSION})")

find_package(nauty REQUIRED)
message(STATUS "Found Nauty: ${NAUTY_LIBRARY} ${NAUTY_INCLUDE_DIR}")

find_package(loki ${LOKI_MIN_VERSION} REQUIRED COMPONENTS parsers)
message(STATUS "Found Loki: ${loki_DIR} (found version ${loki_VERSION})")

find_package(fmt REQUIRED)
message(STATUS "Found fmt: ${fmt_DIR} (found version ${fmt_VERSION})")

find_package(range-v3 REQUIRED)
message(STATUS "Found range-v3: ${range-v3_DIR} (found version ${range-v3_VERSION})")

find_package(cista REQUIRED)
message(STATUS "Found cista: ${cista_DIR} (found version ${cista_VERSION})")

find_package(Threads REQUIRED)
message(STATUS "Found Threads: ${Threads_DIR} (found version ${Threads_VERSION})")


# This prints a summary of found dependencies
include(FeatureSummary)
feature_summary(WHAT ALL)