enable_testing()

find_package(GTest REQUIRED COMPONENTS GTest Main)

set(ALL_TEST_SOURCES)
set(TEST_OBJECT_LIBS)
# Helper function to create and register a test executable
function(configure_test_target test_name)
    target_compile_definitions(${test_name} PUBLIC -DDATA_DIR="${DATA_DIR}")
    target_link_libraries(${test_name} PUBLIC GTest::GTest GTest::Main mimir::mimir)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

function(add_gtest test_name source_file)
    add_library(${test_name}_objects OBJECT ${source_file})
    add_executable(${test_name} $<TARGET_OBJECTS:${test_name}_objects>)
    configure_test_target(${test_name}_objects)
    target_link_libraries(${test_name} PRIVATE ${test_name}_objects)

    set(ALL_TEST_SOURCES "${ALL_TEST_SOURCES};${source_file}" PARENT_SCOPE)
    set(TEST_OBJECT_LIBS "${TEST_OBJECT_LIBS};${test_name}_objects" PARENT_SCOPE)
endfunction()

# Add each test source file as a separate test executable
add_gtest(algorithms_memory_pool_test                      "algorithms/memory_pool.cpp")
add_gtest(algorithms_nauty_test                            "algorithms/nauty.cpp")

add_gtest(cista_dynamic_bitset_test                        "cista/dynamic_bitset.cpp")

add_gtest(common_grouped_vector_test                       "common/grouped_vector.cpp")

add_gtest(datasets_faithful_abstraction_test               "datasets/faithful_abstraction.cpp")
add_gtest(datasets_global_faithful_abstraction_test        "datasets/global_faithful_abstraction.cpp")
add_gtest(datasets_state_space_test                        "datasets/state_space.cpp")

add_gtest(formalism_to_positive_normal_form_test           "formalism/transformers/to_positive_normal_form.cpp")
add_gtest(formalism_to_disjunctive_normal_form_test        "formalism/translators/to_disjunctive_normal_form.cpp")
add_gtest(formalism_to_negation_normal_form_test           "formalism/translators/to_negation_normal_form.cpp")
add_gtest(formalism_remove_types_test                      "formalism/translators/remove_types.cpp")
add_gtest(formalism_remove_universal_quantifiers_test      "formalism/translators/remove_universal_quantifiers.cpp")

add_gtest(graphs_algorithms_color_refinement_test          "graphs/algorithms/color_refinement.cpp")
add_gtest(graphs_algorithms_folklore_weisfeiler_leman_test "graphs/algorithms/folklore_weisfeiler_leman.cpp")
add_gtest(graphs_static_graph_boost_adapter_test           "graphs/static_graph_boost_adapter.cpp")
add_gtest(graphs_dynamic_graph_test                        "graphs/dynamic_graph.cpp")
add_gtest(graphs_object_graph_test                         "graphs/object_graph.cpp")
add_gtest(graphs_object_graph_pruning_strategy_test        "graphs/object_graph_pruning_strategy.cpp")
add_gtest(graphs_static_graph_test                         "graphs/static_graph.cpp")
add_gtest(graphs_tuple_graph_test                          "graphs/tuple_graph.cpp")

add_gtest(languages_dl_constructors_test                   "languages/description_logics/constructors.cpp")
add_gtest(languages_dl_parser_test                         "languages/description_logics/parser.cpp")
add_gtest(languages_dl_grammar_test                        "languages/description_logics/grammar.cpp")

add_gtest(search_astar_test                                "search/algorithms/astar.cpp")
add_gtest(search_brfs_test                                 "search/algorithms/brfs.cpp")
add_gtest(search_iw_test                                   "search/algorithms/iw.cpp")
add_gtest(search_siw_test                                  "search/algorithms/siw.cpp")
add_gtest(search_grounded_test                             "search/applicable_action_generators/grounded.cpp")
add_gtest(search_lifted_test                               "search/applicable_action_generators/lifted.cpp")
add_gtest(search_priority_queue_test                       "search/openlists/priority_queue.cpp")
add_gtest(search_single_test                               "search/planners/single.cpp")
add_gtest(search_search_node_test                          "search/search_node.cpp")
add_gtest(search_state_repository_test                     "search/state_repository.cpp")

message(STATUS "All test sources: ${ALL_TEST_SOURCES}")
add_executable(all_tests)
foreach(obj_lib ${TEST_OBJECT_LIBS})
    target_sources(all_tests PRIVATE $<TARGET_OBJECTS:${obj_lib}>)
endforeach()
configure_test_target(all_tests)

set_target_properties(all_tests PROPERTIES EXCLUDE_FROM_ALL True)
