/*
 * Copyright (C) 2023 Dominik Drexler and Simon Stahlberg
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "mimir/common/conversion.hpp"
#include "mimir/graphs/digraph.hpp"

#include <gtest/gtest.h>

namespace mimir::tests
{

TEST(MimirTests, GraphsStaticDigraphTest)
{
    /**
     * StaticDigraph
     */

    auto graph = StaticDigraph();

    /* Add edge with non existing source or target */
    EXPECT_ANY_THROW(graph.add_directed_edge(0, 0));
    EXPECT_ANY_THROW(graph.add_undirected_edge(1, 2));

    /* Add vertices and edges to the graph. */
    auto v0 = graph.add_vertex();
    auto v1 = graph.add_vertex();
    auto v2 = graph.add_vertex();
    auto v3 = graph.add_vertex();
    auto e0 = graph.add_directed_edge(v0, v1);
    auto [e1, e2] = graph.add_undirected_edge(v1, v2);
    auto e3 = graph.add_directed_edge(v2, v3);
    auto [e4, e5] = graph.add_undirected_edge(v3, v0);

    {
        EXPECT_EQ(graph.get_num_vertices(), 4);
        EXPECT_EQ(graph.get_num_edges(), 6);
        EXPECT_EQ(v0, 0);
        EXPECT_EQ(v1, 1);
        EXPECT_EQ(v2, 2);
        EXPECT_EQ(v3, 3);
        EXPECT_EQ(e0, 0);
        EXPECT_EQ(e1, 1);
        EXPECT_EQ(e2, 2);
        EXPECT_EQ(e3, 3);
        EXPECT_EQ(e4, 4);
        EXPECT_EQ(e5, 5);
        // Non exhaustive test because it should work for others as well.
        EXPECT_EQ(graph.get_source<ForwardTraversal>(e0), v0);
        EXPECT_EQ(graph.get_source<BackwardTraversal>(e0), v1);
        EXPECT_EQ(graph.get_target<ForwardTraversal>(e0), v1);
        EXPECT_EQ(graph.get_target<BackwardTraversal>(e0), v0);
        // Ensure correct out- (ForwardTraversal) and in- (BackwardTraversal) degrees.
        EXPECT_EQ(graph.get_degree<ForwardTraversal>(v0), 2);
        EXPECT_EQ(graph.get_degree<ForwardTraversal>(v1), 1);
        EXPECT_EQ(graph.get_degree<ForwardTraversal>(v2), 2);
        EXPECT_EQ(graph.get_degree<ForwardTraversal>(v3), 1);
        EXPECT_EQ(graph.get_degree<BackwardTraversal>(v0), 1);
        EXPECT_EQ(graph.get_degree<BackwardTraversal>(v1), 2);
        EXPECT_EQ(graph.get_degree<BackwardTraversal>(v2), 1);
        EXPECT_EQ(graph.get_degree<BackwardTraversal>(v3), 2);

        /* Test that iterators yield correct values.
           Non exhaustive test because it should work for other vertices as well. */

        // VertexIndexIterator
        auto vertex_indices = to_set<VertexIndexSet>(graph.get_vertex_indices());
        EXPECT_EQ(vertex_indices.size(), 4);
        EXPECT_TRUE(vertex_indices.contains(v0));
        EXPECT_TRUE(vertex_indices.contains(v1));
        EXPECT_TRUE(vertex_indices.contains(v2));
        EXPECT_TRUE(vertex_indices.contains(v3));

        // EdgeIndexIterator
        auto edge_indices = to_set<EdgeIndexSet>(graph.get_edge_indices());
        EXPECT_EQ(edge_indices.size(), 6);
        EXPECT_TRUE(edge_indices.contains(e0));
        EXPECT_TRUE(edge_indices.contains(e1));
        EXPECT_TRUE(edge_indices.contains(e2));
        EXPECT_TRUE(edge_indices.contains(e3));
        EXPECT_TRUE(edge_indices.contains(e4));
        EXPECT_TRUE(edge_indices.contains(e5));

        // AdjacentVertexIndexIterator
        auto v0_forward_adjacent_vertex_indices = to_set<VertexIndexSet>(graph.get_adjacent_vertex_indices<ForwardTraversal>(v0));
        auto v0_backward_adjacent_vertex_indices = to_set<VertexIndexSet>(graph.get_adjacent_vertex_indices<BackwardTraversal>(v0));

        EXPECT_EQ(v0_forward_adjacent_vertex_indices.size(), 2);
        EXPECT_TRUE(v0_forward_adjacent_vertex_indices.contains(v1));
        EXPECT_TRUE(v0_forward_adjacent_vertex_indices.contains(v3));

        EXPECT_EQ(v0_backward_adjacent_vertex_indices.size(), 1);
        EXPECT_TRUE(v0_backward_adjacent_vertex_indices.contains(v3));

        // AdjacentVertexIterator
        using VertexSetType = unordered_set<typename DynamicDigraph::VertexType>;
        auto v0_foward_adjacent_vertices = to_set<VertexSetType>(graph.get_adjacent_vertices<ForwardTraversal>(v0));
        auto v0_backward_adjacent_vertices = to_set<VertexSetType>(graph.get_adjacent_vertices<BackwardTraversal>(v0));

        EXPECT_EQ(v0_foward_adjacent_vertices.size(), 2);
        EXPECT_TRUE(v0_foward_adjacent_vertices.contains(graph.get_vertices().at(v1)));
        EXPECT_TRUE(v0_foward_adjacent_vertices.contains(graph.get_vertices().at(v3)));

        EXPECT_EQ(v0_backward_adjacent_vertices.size(), 1);
        EXPECT_TRUE(v0_backward_adjacent_vertices.contains(graph.get_vertices().at(v3)));

        // AdjacentEdgeIndexIterator
        auto v0_forward_adjacent_edge_indices = to_set<EdgeIndexSet>(graph.get_adjacent_edge_indices<ForwardTraversal>(v0));
        auto v0_backward_adjacent_edge_indices = to_set<EdgeIndexSet>(graph.get_adjacent_edge_indices<BackwardTraversal>(v0));

        EXPECT_EQ(v0_forward_adjacent_edge_indices.size(), 2);
        EXPECT_TRUE(v0_forward_adjacent_edge_indices.contains(e0));
        EXPECT_TRUE(v0_forward_adjacent_edge_indices.contains(e5));

        EXPECT_EQ(v0_backward_adjacent_edge_indices.size(), 1);
        EXPECT_TRUE(v0_backward_adjacent_edge_indices.contains(e4));

        // AdjacentEdgeIterator
        using EdgeSetType = unordered_set<typename DynamicDigraph::EdgeType>;
        auto v0_forward_adjacent_edge = to_set<EdgeSetType>(graph.get_adjacent_edges<ForwardTraversal>(v0));
        auto v0_backward_adjacent_edge = to_set<EdgeSetType>(graph.get_adjacent_edges<BackwardTraversal>(v0));

        EXPECT_EQ(v0_forward_adjacent_edge.size(), 2);
        EXPECT_TRUE(v0_forward_adjacent_edge.contains(graph.get_edge(e0)));
        EXPECT_TRUE(v0_forward_adjacent_edge.contains(graph.get_edge(e5)));

        EXPECT_EQ(v0_backward_adjacent_edge.size(), 1);
        EXPECT_TRUE(v0_backward_adjacent_edge.contains(graph.get_edge(e4)));
    }

    /**
     * StaticForwardDigraph
     */

    auto forward_graph = StaticForwardDigraph(graph);

    {
        EXPECT_EQ(forward_graph.get_num_vertices(), 4);
        EXPECT_EQ(forward_graph.get_num_edges(), 6);
        // Non exhaustive test because it should work for others as well.
        EXPECT_EQ(forward_graph.get_source<ForwardTraversal>(e0), v0);
        EXPECT_EQ(forward_graph.get_source<BackwardTraversal>(e0), v1);
        EXPECT_EQ(forward_graph.get_target<ForwardTraversal>(e0), v1);
        EXPECT_EQ(forward_graph.get_target<BackwardTraversal>(e0), v0);
        // Ensure correct out- (ForwardTraversal) and in- (BackwardTraversal) degrees.
        EXPECT_EQ(forward_graph.get_degree<ForwardTraversal>(v0), 2);
        EXPECT_EQ(forward_graph.get_degree<ForwardTraversal>(v1), 1);
        EXPECT_EQ(forward_graph.get_degree<ForwardTraversal>(v2), 2);
        EXPECT_EQ(forward_graph.get_degree<ForwardTraversal>(v3), 1);
        EXPECT_EQ(forward_graph.get_degree<BackwardTraversal>(v0), 1);
        EXPECT_EQ(forward_graph.get_degree<BackwardTraversal>(v1), 2);
        EXPECT_EQ(forward_graph.get_degree<BackwardTraversal>(v2), 1);
        EXPECT_EQ(forward_graph.get_degree<BackwardTraversal>(v3), 2);

        /* Test that iterators yield correct values.
           Non exhaustive test because it should work for other vertices as well. */

        // VertexIndexIterator
        auto vertex_indices = to_set<VertexIndexSet>(forward_graph.get_vertex_indices());
        EXPECT_EQ(vertex_indices.size(), 4);
        EXPECT_TRUE(vertex_indices.contains(v0));
        EXPECT_TRUE(vertex_indices.contains(v1));
        EXPECT_TRUE(vertex_indices.contains(v2));
        EXPECT_TRUE(vertex_indices.contains(v3));

        // EdgeIndexIterator
        auto edge_indices = to_set<EdgeIndexSet>(forward_graph.get_edge_indices());
        EXPECT_EQ(edge_indices.size(), 6);
        EXPECT_TRUE(edge_indices.contains(e0));
        EXPECT_TRUE(edge_indices.contains(e1));
        EXPECT_TRUE(edge_indices.contains(e2));
        EXPECT_TRUE(edge_indices.contains(e3));
        EXPECT_TRUE(edge_indices.contains(e4));
        EXPECT_TRUE(edge_indices.contains(e5));

        // AdjacentVertexIndexIterator
        auto v0_forward_adjacent_vertex_indices = to_set<VertexIndexSet>(forward_graph.get_adjacent_vertex_indices<ForwardTraversal>(v0));
        auto v0_backward_adjacent_vertex_indices = to_set<VertexIndexSet>(forward_graph.get_adjacent_vertex_indices<BackwardTraversal>(v0));

        EXPECT_EQ(v0_forward_adjacent_vertex_indices.size(), 2);
        EXPECT_TRUE(v0_forward_adjacent_vertex_indices.contains(v1));
        EXPECT_TRUE(v0_forward_adjacent_vertex_indices.contains(v3));

        EXPECT_EQ(v0_backward_adjacent_vertex_indices.size(), 1);
        EXPECT_TRUE(v0_backward_adjacent_vertex_indices.contains(v3));

        // AdjacentVertexIterator
        using VertexSetType = unordered_set<typename DynamicDigraph::VertexType>;
        auto v0_foward_adjacent_vertices = to_set<VertexSetType>(forward_graph.get_adjacent_vertices<ForwardTraversal>(v0));
        auto v0_backward_adjacent_vertices = to_set<VertexSetType>(forward_graph.get_adjacent_vertices<BackwardTraversal>(v0));

        EXPECT_EQ(v0_foward_adjacent_vertices.size(), 2);
        EXPECT_TRUE(v0_foward_adjacent_vertices.contains(forward_graph.get_vertices().at(v1)));
        EXPECT_TRUE(v0_foward_adjacent_vertices.contains(forward_graph.get_vertices().at(v3)));

        EXPECT_EQ(v0_backward_adjacent_vertices.size(), 1);
        EXPECT_TRUE(v0_backward_adjacent_vertices.contains(forward_graph.get_vertices().at(v3)));

        // AdjacentEdgeIndexIterator
        auto v0_forward_adjacent_edge_indices = to_set<EdgeIndexSet>(forward_graph.get_adjacent_edge_indices<ForwardTraversal>(v0));
        auto v0_backward_adjacent_edge_indices = to_set<EdgeIndexSet>(forward_graph.get_adjacent_edge_indices<BackwardTraversal>(v0));

        EXPECT_EQ(v0_forward_adjacent_edge_indices.size(), 2);
        EXPECT_TRUE(v0_forward_adjacent_edge_indices.contains(e0));
        EXPECT_TRUE(v0_forward_adjacent_edge_indices.contains(e5));

        EXPECT_EQ(v0_backward_adjacent_edge_indices.size(), 1);
        EXPECT_TRUE(v0_backward_adjacent_edge_indices.contains(e4));

        // AdjacentEdgeIterator
        using EdgeSetType = unordered_set<typename DynamicDigraph::EdgeType>;
        auto v0_forward_adjacent_edge = to_set<EdgeSetType>(forward_graph.get_adjacent_edges<ForwardTraversal>(v0));
        auto v0_backward_adjacent_edge = to_set<EdgeSetType>(forward_graph.get_adjacent_edges<BackwardTraversal>(v0));

        EXPECT_EQ(v0_forward_adjacent_edge.size(), 2);
        EXPECT_TRUE(v0_forward_adjacent_edge.contains(forward_graph.get_edge(e0)));
        EXPECT_TRUE(v0_forward_adjacent_edge.contains(forward_graph.get_edge(e5)));

        EXPECT_EQ(v0_backward_adjacent_edge.size(), 1);
        EXPECT_TRUE(v0_backward_adjacent_edge.contains(forward_graph.get_edge(e4)));
    }

    /**
     * StaticBidirectionalDigraph
     */

    auto bidirectional_graph = StaticBidirectionalDigraph(graph);

    {
        EXPECT_EQ(bidirectional_graph.get_num_vertices(), 4);
        EXPECT_EQ(bidirectional_graph.get_num_edges(), 6);
        // Non exhaustive test because it should work for others as well.
        EXPECT_EQ(bidirectional_graph.get_source<ForwardTraversal>(e0), v0);
        EXPECT_EQ(bidirectional_graph.get_source<BackwardTraversal>(e0), v1);
        EXPECT_EQ(bidirectional_graph.get_target<ForwardTraversal>(e0), v1);
        EXPECT_EQ(bidirectional_graph.get_target<BackwardTraversal>(e0), v0);
        // Ensure correct out- (ForwardTraversal) and in- (BackwardTraversal) degrees.
        EXPECT_EQ(bidirectional_graph.get_degree<ForwardTraversal>(v0), 2);
        EXPECT_EQ(bidirectional_graph.get_degree<ForwardTraversal>(v1), 1);
        EXPECT_EQ(bidirectional_graph.get_degree<ForwardTraversal>(v2), 2);
        EXPECT_EQ(bidirectional_graph.get_degree<ForwardTraversal>(v3), 1);
        EXPECT_EQ(bidirectional_graph.get_degree<BackwardTraversal>(v0), 1);
        EXPECT_EQ(bidirectional_graph.get_degree<BackwardTraversal>(v1), 2);
        EXPECT_EQ(bidirectional_graph.get_degree<BackwardTraversal>(v2), 1);
        EXPECT_EQ(bidirectional_graph.get_degree<BackwardTraversal>(v3), 2);

        /* Test that iterators yield correct values.
           Non exhaustive test because it should work for other vertices as well. */

        // VertexIndexIterator
        auto vertex_indices = to_set<VertexIndexSet>(bidirectional_graph.get_vertex_indices());
        EXPECT_EQ(vertex_indices.size(), 4);
        EXPECT_TRUE(vertex_indices.contains(v0));
        EXPECT_TRUE(vertex_indices.contains(v1));
        EXPECT_TRUE(vertex_indices.contains(v2));
        EXPECT_TRUE(vertex_indices.contains(v3));

        // EdgeIndexIterator
        auto edge_indices = to_set<EdgeIndexSet>(bidirectional_graph.get_edge_indices());
        EXPECT_EQ(edge_indices.size(), 6);
        EXPECT_TRUE(edge_indices.contains(e0));
        EXPECT_TRUE(edge_indices.contains(e1));
        EXPECT_TRUE(edge_indices.contains(e2));
        EXPECT_TRUE(edge_indices.contains(e3));
        EXPECT_TRUE(edge_indices.contains(e4));
        EXPECT_TRUE(edge_indices.contains(e5));

        // AdjacentVertexIndexIterator
        auto v0_forward_adjacent_vertex_indices = to_set<VertexIndexSet>(bidirectional_graph.get_adjacent_vertex_indices<ForwardTraversal>(v0));
        auto v0_backward_adjacent_vertex_indices = to_set<VertexIndexSet>(bidirectional_graph.get_adjacent_vertex_indices<BackwardTraversal>(v0));

        EXPECT_EQ(v0_forward_adjacent_vertex_indices.size(), 2);
        EXPECT_TRUE(v0_forward_adjacent_vertex_indices.contains(v1));
        EXPECT_TRUE(v0_forward_adjacent_vertex_indices.contains(v3));

        EXPECT_EQ(v0_backward_adjacent_vertex_indices.size(), 1);
        EXPECT_TRUE(v0_backward_adjacent_vertex_indices.contains(v3));

        // AdjacentVertexIterator
        using VertexSetType = unordered_set<typename DynamicDigraph::VertexType>;
        auto v0_foward_adjacent_vertices = to_set<VertexSetType>(bidirectional_graph.get_adjacent_vertices<ForwardTraversal>(v0));
        auto v0_backward_adjacent_vertices = to_set<VertexSetType>(bidirectional_graph.get_adjacent_vertices<BackwardTraversal>(v0));

        EXPECT_EQ(v0_foward_adjacent_vertices.size(), 2);
        EXPECT_TRUE(v0_foward_adjacent_vertices.contains(bidirectional_graph.get_vertices().at(v1)));
        EXPECT_TRUE(v0_foward_adjacent_vertices.contains(bidirectional_graph.get_vertices().at(v3)));

        EXPECT_EQ(v0_backward_adjacent_vertices.size(), 1);
        EXPECT_TRUE(v0_backward_adjacent_vertices.contains(bidirectional_graph.get_vertices().at(v3)));

        // AdjacentEdgeIndexIterator
        auto v0_forward_adjacent_edge_indices = to_set<EdgeIndexSet>(bidirectional_graph.get_adjacent_edge_indices<ForwardTraversal>(v0));
        auto v0_backward_adjacent_edge_indices = to_set<EdgeIndexSet>(bidirectional_graph.get_adjacent_edge_indices<BackwardTraversal>(v0));

        EXPECT_EQ(v0_forward_adjacent_edge_indices.size(), 2);
        EXPECT_TRUE(v0_forward_adjacent_edge_indices.contains(e0));
        EXPECT_TRUE(v0_forward_adjacent_edge_indices.contains(e5));

        EXPECT_EQ(v0_backward_adjacent_edge_indices.size(), 1);
        EXPECT_TRUE(v0_backward_adjacent_edge_indices.contains(e4));

        // AdjacentEdgeIterator
        using EdgeSetType = unordered_set<typename DynamicDigraph::EdgeType>;
        auto v0_forward_adjacent_edge = to_set<EdgeSetType>(bidirectional_graph.get_adjacent_edges<ForwardTraversal>(v0));
        auto v0_backward_adjacent_edge = to_set<EdgeSetType>(bidirectional_graph.get_adjacent_edges<BackwardTraversal>(v0));

        EXPECT_EQ(v0_forward_adjacent_edge.size(), 2);
        EXPECT_TRUE(v0_forward_adjacent_edge.contains(bidirectional_graph.get_edge(e0)));
        EXPECT_TRUE(v0_forward_adjacent_edge.contains(bidirectional_graph.get_edge(e5)));

        EXPECT_EQ(v0_backward_adjacent_edge.size(), 1);
        EXPECT_TRUE(v0_backward_adjacent_edge.contains(bidirectional_graph.get_edge(e4)));
    }
}

}
