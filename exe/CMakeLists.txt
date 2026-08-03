
add_executable(planner_astar "planner_astar.cpp")
target_link_libraries(planner_astar mimir::core)

add_executable(planner_gbfs "planner_gbfs.cpp")
target_link_libraries(planner_gbfs mimir::core)

add_executable(planner_brfs "planner_brfs.cpp")
target_link_libraries(planner_brfs mimir::core)

add_executable(planner_iw "planner_iw.cpp")
target_link_libraries(planner_iw mimir::core)

add_executable(planner_siw "planner_siw.cpp")
target_link_libraries(planner_siw mimir::core)

add_executable(kb "knowledge_base.cpp")
target_link_libraries(kb mimir::core)

add_executable(planner_astar_symmetries "planner_astar_symmetries.cpp")
target_link_libraries(planner_astar_symmetries mimir::core)
add_executable(probe_parallel_rollouts "probe_parallel_rollouts.cpp")
target_link_libraries(probe_parallel_rollouts mimir::core)

add_executable(bench_serial_iw "bench_serial_iw.cpp")
target_link_libraries(bench_serial_iw mimir::core)
