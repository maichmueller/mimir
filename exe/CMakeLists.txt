add_executable(admissible_chain "admissible_chain.cpp")
target_link_libraries(admissible_chain mimir::mimir)

add_executable(planner_astar "planner_astar.cpp")
target_link_libraries(planner_astar mimir::mimir)

add_executable(planner_brfs "planner_brfs.cpp")
target_link_libraries(planner_brfs mimir::mimir)

add_executable(planner_iw "planner_iw.cpp")
target_link_libraries(planner_iw mimir::mimir)

add_executable(planner_siw "planner_siw.cpp")
target_link_libraries(planner_siw mimir::mimir)

add_executable(gfa "gfa.cpp")
target_link_libraries(gfa mimir::mimir)
