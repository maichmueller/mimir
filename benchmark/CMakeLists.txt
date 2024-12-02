# Set result variables
find_package(benchmark REQUIRED COMPONENTS benchmark main)

add_executable(mimir-profile "profiling.cpp")

target_compile_definitions(mimir-profile PUBLIC -DDATA_DIR="${DATA_DIR}")
target_link_libraries(mimir-profile PRIVATE mimir::mimir benchmark::benchmark benchmark::benchmark_main)
