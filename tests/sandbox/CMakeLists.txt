# ######################################################################################################################
# A mere test-bed executable (hence sandbox)
# ######################################################################################################################

file(GLOB SANDBOX_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" )

add_executable(sandbox ${SANDBOX_TEST_SOURCES})

set_target_properties(
        sandbox PROPERTIES EXCLUDE_FROM_ALL True # don't build the sandbox when ALL is asked to be built. Only on demand.
)

target_include_directories(
        sandbox
        PRIVATE
        ${PROJECT_SOURCE_DIR}/python/src/pymimir/_bindings
)

target_link_libraries(
        sandbox
        PRIVATE
        ${reinforce_lib}
        mimir::mimir
)
if(BUILD_PYMIMIR)
    find_package(pybind11 CONFIG REQUIRED)
    target_link_libraries(
            sandbox
            PUBLIC
            pybind11::headers
            pybind11::embed
            pybind11::lto
    )
endif ()