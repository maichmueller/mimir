# List of subdirectory names
set(SUBDIRECTORIES "algorithms" "common" "datasets" "formalism" "graphs" "search" "languages")

# Initialize empty lists for source and header files
set(MIMIR_SRC_FILES "")
set(MIMIR_PRIVATE_HEADER_FILES "")
set(MIMIR_PUBLIC_HEADER_FILES "")

# Loop over the subdirectories for source files
foreach (subdir ${SUBDIRECTORIES})
    file(
            GLOB_RECURSE
            _tmp_src_files
            "${subdir}/*.cpp"
            "${subdir}/**/*.cpp"
    )
    list(APPEND MIMIR_SRC_FILES ${_tmp_src_files})

    file(
            GLOB_RECURSE
            _tmp_private_header_files
            "${subdir}/*.hpp"
            "${subdir}/**/*.hpp"
    )
    list(APPEND MIMIR_PRIVATE_HEADER_FILES ${_tmp_private_header_files})

    file(
            GLOB_RECURSE
            _tmp_public_header_files
            "${PROJECT_SOURCE_DIR}/include/mimir/${subdir}/*.hpp"
            "${PROJECT_SOURCE_DIR}/include/mimir/${subdir}/**/*.hpp"
    )
    list(APPEND MIMIR_PUBLIC_HEADER_FILES ${_tmp_public_header_files})
endforeach ()

list(APPEND MIMIR_PUBLIC_HEADER_FILES "${PROJECT_SOURCE_DIR}/include/mimir/mimir.hpp")

add_library(mimir-headers INTERFACE)

target_include_directories(
        mimir-headers
        INTERFACE
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

add_library(
        mimir
        STATIC
        ${MIMIR_SRC_FILES}
        ${MIMIR_PRIVATE_HEADER_FILES}
)
target_link_libraries(mimir PUBLIC mimir-headers)

add_library(mimir::headers ALIAS mimir-headers)
add_library(mimir::mimir ALIAS mimir)

# Apply the compile definition based on the selected representation
if (STATE_REPR STREQUAL "Dense")
    target_compile_definitions(mimir PUBLIC STATE_REPR_DENSE=1)
endif ()
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(mimir PUBLIC "$<BUILD_INTERFACE:-fconcepts-diagnostics-depth=10>")
endif ()

set(_link_libs
        cista::cista
        loki::loki
        nauty::nauty
        Boost::iostreams
        ZLIB::ZLIB
        fmt::fmt
        range-v3::range-v3
        Threads::Threads
)
target_link_libraries(
        mimir
        PUBLIC
        ${_link_libs}
)
target_link_libraries(
        mimir-headers
        INTERFACE
        ${_link_libs}
)

# Use include depending on building or using from installed location
target_include_directories(mimir
        PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

# Install the target and create export-set
install(
        TARGETS mimir mimir-headers
        EXPORT mimirTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Generate and install export file
install(EXPORT mimirTargets
        NAMESPACE mimir::
        FILE "mimirTargets.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mimir"
)
