#include "mimir/graphs/object_graph.hpp"

#include "mimir/datasets/state_space.hpp"

#include <gtest/gtest.h>

namespace mimir::tests
{

TEST(MimirTests, GraphsObjectGraphTest)
{
    const auto domain_file = fs::path(std::string(DATA_DIR) + "gripper/domain.pddl");
    const auto problem_file = fs::path(std::string(DATA_DIR) + "gripper/p-2-0.pddl");

    const auto state_space = StateSpaceImpl::create(domain_file, problem_file, 10000, 10000);

    auto object_graph_factory = ObjectGraphFactory(state_space->get_problem());

    auto nauty_graph = nauty_wrapper::Graph();

    auto certificates = std::unordered_set<std::string> {};

    for (const auto& state : state_space->get_states())
    {
        // std::cout << std::make_tuple(state_space->get_aag()->get_problem(), state, std::cref(state_space->get_aag()->get_pddl_factories())) << std::endl;

        const auto& object_graph = object_graph_factory.create(state, state_space->get_factories());

        // std::cout << object_graph << std::endl;

        object_graph.get_digraph().to_nauty_graph(nauty_graph);
        const auto certificate = nauty_graph.compute_certificate(object_graph.get_lab(), object_graph.get_ptn());
        const auto& sorted_vertex_coloring = object_graph.get_sorted_vertex_colors();

        std::stringstream ss;
        ss << certificate << " " << sorted_vertex_coloring;

        certificates.insert(ss.str());
    }

    EXPECT_EQ(state_space->get_states().size(), 28);
    EXPECT_EQ(certificates.size(), 12);
}
}
