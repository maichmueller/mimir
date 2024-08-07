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

#ifndef MIMIR_GRAPHS_STATIC_GRAPH_HPP_
#define MIMIR_GRAPHS_STATIC_GRAPH_HPP_

#include "mimir/common/concepts.hpp"
#include "mimir/common/grouped_vector.hpp"
#include "mimir/graphs/graph_edge_interface.hpp"
#include "mimir/graphs/graph_vertex_interface.hpp"
#include "mimir/graphs/static_graph_interface.hpp"
#include "mimir/graphs/static_graph_iterators.hpp"

#include <ranges>
#include <span>
#include <vector>

namespace mimir
{

/**
 * Declarations
 */

/* StaticGraph */

/// @brief `StaticGraph` G implements a static version of a directed graph with vertices V and
/// edges E satisfying the graph concepts:
///   1) VertexListGraph,
///   2) EdgeListGraph,
///   3) IncidenceGraph, and
///   4) AdjacencyGraph.
/// The meaning of static is that the graph allows for the addition but not for the removal of
/// vertices and edges.
/// Due to the restriction to addition functionality, the implementation uses more efficient vector
/// data structures compared to `DynamicGraph`.
///
/// `StaticGraph` supports traversal of adjacent vertices and edges in forward and backward
/// directions. The iterators filter adjacent vertices or edges of a vertex from all edges.
/// This is not efficient and unavoidable because adjacent edges cannot be grouped efficiently
/// when allowing for the addition of edges.
/// The iteration in forward or backward directions by translating a `StaticGraph` to a
/// StaticForwardGraph, optimized for forward traversal, or a StaticBidirectionalGraph, optimized
/// for forward and backward traversal, both in O(|V|+|E|*Log2(|E|)) with minimal memory overhead.
/// @tparam Vertex is vertex type.
/// @tparam Edge is the edge type.
template<IsVertex Vertex, IsEdge Edge>
class StaticGraph
{
public:
    using GraphTag = StaticGraphTag;
    using VertexType = Vertex;
    using VertexList = std::vector<Vertex>;
    using EdgeType = Edge;
    using EdgeList = std::vector<Edge>;

    using VertexIndexConstIteratorType = std::ranges::iterator_t<std::ranges::iota_view<VertexIndex, VertexIndex>>;
    using EdgeIndexConstIteratorType = std::ranges::iterator_t<std::ranges::iota_view<EdgeIndex, EdgeIndex>>;

    template<IsTraversalDirection Direction>
    using AdjacentVertexConstIteratorType = StaticAdjacentVertexConstIterator<Vertex, Edge, Direction>;
    template<IsTraversalDirection Direction>
    using AdjacentVertexIndexConstIteratorType = StaticAdjacentVertexIndexConstIterator<Edge, Direction>;
    template<IsTraversalDirection Direction>
    using AdjacentEdgeConstIteratorType = StaticAdjacentEdgeConstIterator<Edge, Direction>;
    template<IsTraversalDirection Direction>
    using AdjacentEdgeIndexConstIteratorType = StaticAdjacentEdgeIndexConstIterator<Edge, Direction>;

    /// @brief Construct an empty graph.
    StaticGraph();

    /// @brief Reinitialize the graph to an empty graph.
    void clear();

    /**
     * Constructible functionality.
     */

    /// @brief Add a vertex to the graph with vertex properties args.
    /// @tparam ...VertexProperties the types of the vertex properties. Must match the properties mentioned in the vertex constructor.
    /// @param ...properties the vertex properties.
    /// @return the index of the newly created vertex.
    template<typename... VertexProperties>
    VertexIndex add_vertex(VertexProperties&&... properties);

    /// @brief Add a directed edge from source to target to the graph with edge properties args.
    /// @tparam ...Args the types of the edge properties. Must match the properties mentioned in the edge constructor.
    /// @param source the source vertex.
    /// @param target the target vertex.
    /// @param ...properties the edge properties.
    /// @return the index of the newly created edge.
    template<typename... EdgeProperties>
    EdgeIndex add_directed_edge(VertexIndex source, VertexIndex target, EdgeProperties&&... properties);

    /// @brief Add two anti-parallel directed edges to the graph with the identical edge properties, representing the undirected edge.
    /// If the edge properties are heavy weight, we suggest externalizing the properties and storing an index to the properties instead.
    /// @tparam ...EdgeProperties the types of the edge properties. Must match the properties mentioned in the edge constructor.
    /// @param source the source vertex.
    /// @param target the target vertex.
    /// @param ...properties the edge properties.
    /// @return the index pair of the two newly created edges.
    template<typename... EdgeProperties>
    std::pair<EdgeIndex, EdgeIndex> add_undirected_edge(VertexIndex source, VertexIndex target, EdgeProperties&&... properties);

    /**
     * Iterators
     */

    std::ranges::subrange<VertexIndexConstIteratorType> get_vertex_indices() const;
    std::ranges::subrange<EdgeIndexConstIteratorType> get_edge_indices() const;

    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentVertexConstIteratorType<Direction>> get_adjacent_vertices(VertexIndex vertex) const;
    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentVertexIndexConstIteratorType<Direction>> get_adjacent_vertex_indices(VertexIndex vertex) const;
    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentEdgeConstIteratorType<Direction>> get_adjacent_edges(VertexIndex vertex) const;
    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentEdgeIndexConstIteratorType<Direction>> get_adjacent_edge_indices(VertexIndex vertex) const;

    /**
     * Getters
     */

    const VertexList& get_vertices() const;
    const Vertex& get_vertex(VertexIndex vertex) const;
    const EdgeList& get_edges() const;
    const Edge& get_edge(EdgeIndex edge) const;
    size_t get_num_vertices() const;
    size_t get_num_edges() const;

    template<IsTraversalDirection>
    VertexIndex get_source(EdgeIndex edge) const;
    template<IsTraversalDirection>
    VertexIndex get_target(EdgeIndex edge) const;
    template<IsTraversalDirection Direction>
    const DegreeList& get_degrees() const;
    template<IsTraversalDirection Direction>
    Degree get_degree(VertexIndex vertex) const;

private:
    VertexList m_vertices;
    EdgeList m_edges;

    TraversalDirectionStorage<DegreeList> m_degrees;

    // Slice over all edge indices for using the iterators.
    EdgeIndexList m_slice;

    /**
     * Error handling
     */

    void vertex_index_check(VertexIndex vertex, const std::string& error_message) const;
    void edge_index_check(EdgeIndex edge, const std::string& error_message) const;
};

/* StaticForwardGraph */

/// @brief `StaticForwardGraph` is a translated `StaticGraph` for more efficient forward traversal.
template<IsStaticGraph G>
class StaticForwardGraph
{
public:
    using GraphTag = typename G::GraphTag;
    using VertexType = typename G::VertexType;
    using EdgeType = typename G::EdgeType;
    using VertexList = std::vector<VertexType>;
    using EdgeList = std::vector<EdgeType>;

    using VertexIndexConstIteratorType = typename G::VertexIndexConstIteratorType;
    using EdgeIndexConstIteratorType = typename G::EdgeIndexConstIteratorType;

    template<IsTraversalDirection Direction>
    using AdjacentVertexConstIteratorType = typename G::template AdjacentVertexConstIteratorType<Direction>;
    template<IsTraversalDirection Direction>
    using AdjacentVertexIndexConstIteratorType = typename G::template AdjacentVertexIndexConstIteratorType<Direction>;
    template<IsTraversalDirection Direction>
    using AdjacentEdgeConstIteratorType = typename G::template AdjacentEdgeConstIteratorType<Direction>;
    template<IsTraversalDirection Direction>
    using AdjacentEdgeIndexConstIteratorType = typename G::template AdjacentEdgeIndexConstIteratorType<Direction>;

    explicit StaticForwardGraph(G graph);

    /**
     * Iterators
     */

    std::ranges::subrange<VertexIndexConstIteratorType> get_vertex_indices() const;
    std::ranges::subrange<EdgeIndexConstIteratorType> get_edge_indices() const;

    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentVertexConstIteratorType<Direction>> get_adjacent_vertices(VertexIndex vertex) const;
    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentVertexIndexConstIteratorType<Direction>> get_adjacent_vertex_indices(VertexIndex vertex) const;
    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentEdgeConstIteratorType<Direction>> get_adjacent_edges(VertexIndex vertex) const;
    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentEdgeIndexConstIteratorType<Direction>> get_adjacent_edge_indices(VertexIndex vertex) const;

    /**
     * Getters
     */

    const VertexList& get_vertices() const;
    const VertexType& get_vertex(VertexIndex vertex) const;
    const EdgeList& get_edges() const;
    const EdgeType& get_edge(EdgeIndex edge) const;
    size_t get_num_vertices() const;
    size_t get_num_edges() const;

    template<IsTraversalDirection>
    VertexIndex get_source(EdgeIndex edge) const;
    template<IsTraversalDirection>
    VertexIndex get_target(EdgeIndex edge) const;
    template<IsTraversalDirection Direction>
    const DegreeList& get_degrees() const;
    template<IsTraversalDirection Direction>
    Degree get_degree(VertexIndex vertex) const;

private:
    G m_graph;

    IndexGroupedVector<const EdgeIndex> m_edge_indices_grouped_by_source;
};

/* BidirectionalGraph */

/// @brief `BidirectionalGraph` is a translated `StaticGraph` for more efficient bidirectional traversal.
template<IsStaticGraph G>
class StaticBidirectionalGraph
{
public:
    using GraphTag = typename G::GraphTag;
    using VertexType = typename G::VertexType;
    using EdgeType = typename G::EdgeType;
    using VertexList = std::vector<VertexType>;
    using EdgeList = std::vector<EdgeType>;

    using VertexIndexConstIteratorType = typename G::VertexIndexConstIteratorType;
    using EdgeIndexConstIteratorType = typename G::EdgeIndexConstIteratorType;

    template<IsTraversalDirection Direction>
    using AdjacentVertexConstIteratorType = typename G::template AdjacentVertexConstIteratorType<Direction>;
    template<IsTraversalDirection Direction>
    using AdjacentVertexIndexConstIteratorType = typename G::template AdjacentVertexIndexConstIteratorType<Direction>;
    template<IsTraversalDirection Direction>
    using AdjacentEdgeConstIteratorType = typename G::template AdjacentEdgeConstIteratorType<Direction>;
    template<IsTraversalDirection Direction>
    using AdjacentEdgeIndexConstIteratorType = typename G::template AdjacentEdgeIndexConstIteratorType<Direction>;

    explicit StaticBidirectionalGraph(G graph);

    /**
     * Iterators
     */

    std::ranges::subrange<VertexIndexConstIteratorType> get_vertex_indices() const;
    std::ranges::subrange<EdgeIndexConstIteratorType> get_edge_indices() const;

    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentVertexConstIteratorType<Direction>> get_adjacent_vertices(VertexIndex vertex) const;
    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentVertexIndexConstIteratorType<Direction>> get_adjacent_vertex_indices(VertexIndex vertex) const;
    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentEdgeConstIteratorType<Direction>> get_adjacent_edges(VertexIndex vertex) const;
    template<IsTraversalDirection Direction>
    std::ranges::subrange<AdjacentEdgeIndexConstIteratorType<Direction>> get_adjacent_edge_indices(VertexIndex vertex) const;

    /**
     * Getters
     */

    const VertexList& get_vertices() const;
    const VertexType& get_vertex(VertexIndex vertex) const;
    const EdgeList& get_edges() const;
    const EdgeType& get_edge(EdgeIndex edge) const;
    size_t get_num_vertices() const;
    size_t get_num_edges() const;

    template<IsTraversalDirection>
    VertexIndex get_source(EdgeIndex edge) const;
    template<IsTraversalDirection>
    VertexIndex get_target(EdgeIndex edge) const;
    template<IsTraversalDirection Direction>
    const DegreeList& get_degrees() const;
    template<IsTraversalDirection Direction>
    Degree get_degree(VertexIndex vertex) const;

private:
    G m_graph;

    TraversalDirectionStorage<IndexGroupedVector<const EdgeIndex>> m_edge_indices_grouped_by_vertex;
};

/**
 * Implementations
 */

/* StaticGraph */

template<IsVertex Vertex, IsEdge Edge>
StaticGraph<Vertex, Edge>::StaticGraph() : m_vertices(), m_edges(), m_degrees()
{
}

template<IsVertex Vertex, IsEdge Edge>
template<typename... VertexProperties>
VertexIndex StaticGraph<Vertex, Edge>::add_vertex(VertexProperties&&... properties)
{
    const auto index = m_vertices.size();
    m_vertices.emplace_back(index, std::forward<VertexProperties>(properties)...);
    m_degrees.get<ForwardTraversal>().resize(index + 1, 0);
    m_degrees.get<BackwardTraversal>().resize(index + 1, 0);

    return index;
}

template<IsVertex Vertex, IsEdge Edge>
template<typename... EdgeProperties>
EdgeIndex StaticGraph<Vertex, Edge>::add_directed_edge(VertexIndex source, VertexIndex target, EdgeProperties&&... properties)
{
    vertex_index_check(source, "StaticGraph<Vertex, Edge>::add_directed_edge(...): Source vertex out of range");
    vertex_index_check(target, "StaticGraph<Vertex, Edge>::add_directed_edge(...): Source vertex out of range");

    const auto index = m_edges.size();
    m_edges.emplace_back(index, source, target, std::forward<EdgeProperties>(properties)...);
    ++m_degrees.get<ForwardTraversal>().at(source);
    ++m_degrees.get<BackwardTraversal>().at(target);
    m_slice.push_back(index);

    return index;
}

template<IsVertex Vertex, IsEdge Edge>
template<typename... EdgeProperties>
std::pair<EdgeIndex, EdgeIndex> StaticGraph<Vertex, Edge>::add_undirected_edge(VertexIndex source, VertexIndex target, EdgeProperties&&... properties)
{
    // Need to copy properties to keep them in valid state.
    const auto forward_edge_index = add_directed_edge(source, target, properties...);
    const auto backward_edge_index = add_directed_edge(target, source, std::forward<EdgeProperties>(properties)...);

    return std::make_pair(forward_edge_index, backward_edge_index);
}

template<IsVertex Vertex, IsEdge Edge>
void StaticGraph<Vertex, Edge>::clear()
{
    m_vertices.clear();
    m_edges.clear();
    m_degrees.get<ForwardTraversal>().clear();
    m_degrees.get<BackwardTraversal>().clear();
    m_slice.clear();
}

template<IsVertex Vertex, IsEdge Edge>
std::ranges::subrange<typename StaticGraph<Vertex, Edge>::VertexIndexConstIteratorType> StaticGraph<Vertex, Edge>::get_vertex_indices() const
{
    auto range = std::ranges::iota_view<VertexIndex, VertexIndex>(0, get_num_vertices());
    static_assert(std::ranges::borrowed_range<decltype(range)>);
    return std::ranges::subrange<VertexIndexConstIteratorType>(range.begin(), range.end());
}

template<IsVertex Vertex, IsEdge Edge>
std::ranges::subrange<typename StaticGraph<Vertex, Edge>::EdgeIndexConstIteratorType> StaticGraph<Vertex, Edge>::get_edge_indices() const
{
    auto range = std::ranges::iota_view<EdgeIndex, EdgeIndex>(0, get_num_edges());
    static_assert(std::ranges::borrowed_range<decltype(range)>);
    return std::ranges::subrange<EdgeIndexConstIteratorType>(range.begin(), range.end());
}

template<IsVertex Vertex, IsEdge Edge>
template<IsTraversalDirection Direction>
std::ranges::subrange<typename StaticGraph<Vertex, Edge>::template AdjacentVertexConstIteratorType<Direction>>
StaticGraph<Vertex, Edge>::get_adjacent_vertices(VertexIndex vertex) const
{
    vertex_index_check(vertex, "StaticGraph<Vertex, Edge>::get_adjacent_vertices(...): Vertex out of range");

    return std::ranges::subrange(typename StaticGraph<Vertex, Edge>::AdjacentVertexConstIteratorType<Direction>(vertex, m_vertices, m_edges, m_slice, true),
                                 typename StaticGraph<Vertex, Edge>::AdjacentVertexConstIteratorType<Direction>(vertex, m_vertices, m_edges, m_slice, false));
}

template<IsVertex Vertex, IsEdge Edge>
template<IsTraversalDirection Direction>
std::ranges::subrange<typename StaticGraph<Vertex, Edge>::template AdjacentVertexIndexConstIteratorType<Direction>>
StaticGraph<Vertex, Edge>::get_adjacent_vertex_indices(VertexIndex vertex) const
{
    vertex_index_check(vertex, "StaticGraph<Vertex, Edge>::get_adjacent_vertex_indices(...): Vertex out of range");

    return std::ranges::subrange(typename StaticGraph<Vertex, Edge>::AdjacentVertexIndexConstIteratorType<Direction>(vertex, m_edges, m_slice, true),
                                 typename StaticGraph<Vertex, Edge>::AdjacentVertexIndexConstIteratorType<Direction>(vertex, m_edges, m_slice, false));
}

template<IsVertex Vertex, IsEdge Edge>
template<IsTraversalDirection Direction>
std::ranges::subrange<typename StaticGraph<Vertex, Edge>::template AdjacentEdgeConstIteratorType<Direction>>
StaticGraph<Vertex, Edge>::get_adjacent_edges(VertexIndex vertex) const
{
    vertex_index_check(vertex, "StaticGraph<Vertex, Edge>::get_adjacent_edges(...): Vertex out of range");

    return std::ranges::subrange(typename StaticGraph<Vertex, Edge>::AdjacentEdgeConstIteratorType<Direction>(vertex, m_edges, m_slice, true),
                                 typename StaticGraph<Vertex, Edge>::AdjacentEdgeConstIteratorType<Direction>(vertex, m_edges, m_slice, false));
}

template<IsVertex Vertex, IsEdge Edge>
template<IsTraversalDirection Direction>
std::ranges::subrange<typename StaticGraph<Vertex, Edge>::template AdjacentEdgeIndexConstIteratorType<Direction>>
StaticGraph<Vertex, Edge>::get_adjacent_edge_indices(VertexIndex vertex) const
{
    vertex_index_check(vertex, "StaticGraph<Vertex, Edge>::get_adjacent_edge_indices(...): Vertex out of range");

    return std::ranges::subrange(typename StaticGraph<Vertex, Edge>::AdjacentEdgeIndexConstIteratorType<Direction>(vertex, m_edges, m_slice, true),
                                 typename StaticGraph<Vertex, Edge>::AdjacentEdgeIndexConstIteratorType<Direction>(vertex, m_edges, m_slice, false));
}

template<IsVertex Vertex, IsEdge Edge>
const StaticGraph<Vertex, Edge>::VertexList& StaticGraph<Vertex, Edge>::get_vertices() const
{
    return m_vertices;
}

template<IsVertex Vertex, IsEdge Edge>
const Vertex& StaticGraph<Vertex, Edge>::get_vertex(VertexIndex vertex) const
{
    vertex_index_check(vertex, "StaticGraph<Vertex, Edge>::get_vertex(...): Vertex does not exist.");

    return m_vertices[vertex];
}

template<IsVertex Vertex, IsEdge Edge>
const StaticGraph<Vertex, Edge>::EdgeList& StaticGraph<Vertex, Edge>::get_edges() const
{
    return m_edges;
}

template<IsVertex Vertex, IsEdge Edge>
const Edge& StaticGraph<Vertex, Edge>::get_edge(EdgeIndex edge) const
{
    edge_index_check(edge, "StaticGraph<Vertex, Edge>::get_edge(...): Edge does not exist.");

    return m_edges[edge];
}

template<IsVertex Vertex, IsEdge Edge>
size_t StaticGraph<Vertex, Edge>::get_num_vertices() const
{
    return m_vertices.size();
}

template<IsVertex Vertex, IsEdge Edge>
size_t StaticGraph<Vertex, Edge>::get_num_edges() const
{
    return m_edges.size();
}

template<IsVertex Vertex, IsEdge Edge>
template<IsTraversalDirection Direction>
VertexIndex StaticGraph<Vertex, Edge>::get_source(EdgeIndex edge) const
{
    edge_index_check(edge, "StaticGraph<Vertex, Edge>::get_source(...): Edge out of range");

    if constexpr (std::is_same_v<Direction, ForwardTraversal>)
    {
        return m_edges[edge].get_source();
    }
    else if constexpr (std::is_same_v<Direction, BackwardTraversal>)
    {
        return m_edges[edge].get_target();
    }
    else
    {
        static_assert(dependent_false<Direction>::value, "StaticGraph<Vertex, Edge>::get_source(...): Missing implementation for IsTraversalDirection.");
    }
}

template<IsVertex Vertex, IsEdge Edge>
template<IsTraversalDirection Direction>
VertexIndex StaticGraph<Vertex, Edge>::get_target(EdgeIndex edge) const
{
    edge_index_check(edge, "StaticGraph<Vertex, Edge>::get_target(...): Edge out of range");

    if constexpr (std::is_same_v<Direction, ForwardTraversal>)
    {
        return m_edges[edge].get_target();
    }
    else if constexpr (std::is_same_v<Direction, BackwardTraversal>)
    {
        return m_edges[edge].get_source();
    }
    else
    {
        static_assert(dependent_false<Direction>::value, "StaticGraph<Vertex, Edge>::get_target(...): Missing implementation for IsTraversalDirection.");
    }
}

template<IsVertex Vertex, IsEdge Edge>
template<IsTraversalDirection Direction>
const DegreeList& StaticGraph<Vertex, Edge>::get_degrees() const
{
    return m_degrees.get<Direction>();
}

template<IsVertex Vertex, IsEdge Edge>
template<IsTraversalDirection Direction>
Degree StaticGraph<Vertex, Edge>::get_degree(VertexIndex vertex) const
{
    vertex_index_check(vertex, "StaticGraph<Vertex, Edge>::get_degree(...): Vertex out of range");

    return m_degrees.get<Direction>()[vertex];
}

template<IsVertex Vertex, IsEdge Edge>
void StaticGraph<Vertex, Edge>::vertex_index_check(VertexIndex vertex, const std::string& error_message) const
{
    if (vertex >= get_num_vertices() || vertex < 0)
    {
        throw std::out_of_range(error_message);
    }
}

template<IsVertex Vertex, IsEdge Edge>
void StaticGraph<Vertex, Edge>::edge_index_check(EdgeIndex edge, const std::string& error_message) const
{
    if (edge >= get_num_edges() || edge < 0)
    {
        throw std::out_of_range(error_message);
    }
}

/* StaticForwardGraph */

/// @brief Groups edge indices by source or target, depending on forward is true or false.
/// @tparam Vertex is the type of vertices in the graph.
/// @tparam Edge is the type of edges in the graph.
/// @param graph is the graph.
/// @param forward true will group by source and false will group by target.
/// @return
template<IsVertex Vertex, IsEdge Edge>
static IndexGroupedVector<const EdgeIndex> compute_index_grouped_edge_indices(const StaticGraph<Vertex, Edge>& graph, bool forward)
{
    auto grouped_by_source_data = std::vector<std::pair<VertexIndex, EdgeIndex>> {};
    for (const auto& edge : graph.get_edges())
    {
        grouped_by_source_data.emplace_back(((forward) ? edge.get_source() : edge.get_target()), edge.get_index());
    }

    std::sort(grouped_by_source_data.begin(), grouped_by_source_data.end(), [](const auto& l, const auto& r) { return l.first < r.first; });

    auto index_grouped_by_source = IndexGroupedVector<std::pair<VertexIndex, EdgeIndex>>::create(
        std::move(grouped_by_source_data),
        [](const auto& l, const auto& r) { return l.first != r.first; },
        [](const auto& e) { return e.first; },
        graph.get_num_vertices());

    auto edge_index_grouped_by_source_builder = IndexGroupedVectorBuilder<const EdgeIndex>();
    for (const auto& group : index_grouped_by_source)
    {
        edge_index_grouped_by_source_builder.start_group();

        for (const auto& element : group)
        {
            edge_index_grouped_by_source_builder.add_group_element(element.second);
        }
    }
    return edge_index_grouped_by_source_builder.get_result();
}

template<IsStaticGraph G>
StaticForwardGraph<G>::StaticForwardGraph(G graph) :
    m_graph(std::move(graph)),
    m_edge_indices_grouped_by_source(compute_index_grouped_edge_indices(m_graph, true))
{
}

template<IsStaticGraph G>
std::ranges::subrange<typename StaticForwardGraph<G>::VertexIndexConstIteratorType> StaticForwardGraph<G>::get_vertex_indices() const
{
    return m_graph.get_vertex_indices();
}

template<IsStaticGraph G>
std::ranges::subrange<typename StaticForwardGraph<G>::EdgeIndexConstIteratorType> StaticForwardGraph<G>::get_edge_indices() const
{
    return m_graph.get_edge_indices();
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
std::ranges::subrange<typename StaticForwardGraph<G>::template AdjacentVertexConstIteratorType<Direction>>
StaticForwardGraph<G>::get_adjacent_vertices(VertexIndex vertex) const
{
    if constexpr (std::is_same_v<Direction, ForwardTraversal>)
    {
        return std::ranges::subrange(
            typename StaticForwardGraph<G>::AdjacentVertexConstIteratorType<ForwardTraversal>(vertex,
                                                                                              m_graph.get_vertices(),
                                                                                              m_graph.get_edges(),
                                                                                              m_edge_indices_grouped_by_source.at(vertex),
                                                                                              true),
            typename StaticForwardGraph<G>::AdjacentVertexConstIteratorType<ForwardTraversal>(vertex,
                                                                                              m_graph.get_vertices(),
                                                                                              m_graph.get_edges(),
                                                                                              m_edge_indices_grouped_by_source.at(vertex),
                                                                                              false));
    }
    else if constexpr (std::is_same_v<Direction, BackwardTraversal>)
    {
        return m_graph.template get_adjacent_vertices<BackwardTraversal>(vertex);
    }
    else
    {
        static_assert(dependent_false<Direction>::value, "StaticForwardGraph<G>::get_adjacent_vertices: Missing implementation for IsTraversalDirection.");
    }
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
std::ranges::subrange<typename StaticForwardGraph<G>::template AdjacentVertexIndexConstIteratorType<Direction>>
StaticForwardGraph<G>::get_adjacent_vertex_indices(VertexIndex vertex) const
{
    if constexpr (std::is_same_v<Direction, ForwardTraversal>)
    {
        return std::ranges::subrange(
            typename StaticForwardGraph<G>::AdjacentVertexIndexConstIteratorType<ForwardTraversal>(vertex,
                                                                                                   m_graph.get_edges(),
                                                                                                   m_edge_indices_grouped_by_source.at(vertex),
                                                                                                   true),
            typename StaticForwardGraph<G>::AdjacentVertexIndexConstIteratorType<ForwardTraversal>(vertex,
                                                                                                   m_graph.get_edges(),
                                                                                                   m_edge_indices_grouped_by_source.at(vertex),
                                                                                                   false));
    }
    else if constexpr (std::is_same_v<Direction, BackwardTraversal>)
    {
        return m_graph.template get_adjacent_vertex_indices<BackwardTraversal>(vertex);
    }
    else
    {
        static_assert(dependent_false<Direction>::value,
                      "StaticForwardGraph<G>::get_adjacent_vertex_indices: Missing implementation for IsTraversalDirection.");
    }
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
std::ranges::subrange<typename StaticForwardGraph<G>::template AdjacentEdgeConstIteratorType<Direction>>
StaticForwardGraph<G>::get_adjacent_edges(VertexIndex vertex) const
{
    if constexpr (std::is_same_v<Direction, ForwardTraversal>)
    {
        return std::ranges::subrange(
            typename StaticForwardGraph<G>::AdjacentEdgeConstIteratorType<ForwardTraversal>(vertex,
                                                                                            m_graph.get_edges(),
                                                                                            m_edge_indices_grouped_by_source.at(vertex),
                                                                                            true),
            typename StaticForwardGraph<G>::AdjacentEdgeConstIteratorType<ForwardTraversal>(vertex,
                                                                                            m_graph.get_edges(),
                                                                                            m_edge_indices_grouped_by_source.at(vertex),
                                                                                            false));
    }
    else if constexpr (std::is_same_v<Direction, BackwardTraversal>)
    {
        return m_graph.template get_adjacent_edges<BackwardTraversal>(vertex);
    }
    else
    {
        static_assert(dependent_false<Direction>::value, "StaticForwardGraph<G>::get_adjacent_edges: Missing implementation for IsTraversalDirection.");
    }
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
std::ranges::subrange<typename StaticForwardGraph<G>::template AdjacentEdgeIndexConstIteratorType<Direction>>
StaticForwardGraph<G>::get_adjacent_edge_indices(VertexIndex vertex) const
{
    if constexpr (std::is_same_v<Direction, ForwardTraversal>)
    {
        return std::ranges::subrange(
            typename StaticForwardGraph<G>::AdjacentEdgeIndexConstIteratorType<ForwardTraversal>(vertex,
                                                                                                 m_graph.get_edges(),
                                                                                                 m_edge_indices_grouped_by_source.at(vertex),
                                                                                                 true),
            typename StaticForwardGraph<G>::AdjacentEdgeIndexConstIteratorType<ForwardTraversal>(vertex,
                                                                                                 m_graph.get_edges(),
                                                                                                 m_edge_indices_grouped_by_source.at(vertex),
                                                                                                 false));
    }
    else if constexpr (std::is_same_v<Direction, BackwardTraversal>)
    {
        return m_graph.template get_adjacent_edge_indices<BackwardTraversal>(vertex);
    }
    else
    {
        static_assert(dependent_false<Direction>::value, "StaticForwardGraph<G>::get_adjacent_edge_indices: Missing implementation for IsTraversalDirection.");
    }
}

template<IsStaticGraph G>
const StaticForwardGraph<G>::VertexList& StaticForwardGraph<G>::get_vertices() const
{
    return m_graph.get_vertices();
}

template<IsStaticGraph G>
const StaticForwardGraph<G>::VertexType& StaticForwardGraph<G>::get_vertex(VertexIndex vertex) const
{
    return m_graph.get_vertex(vertex);
}

template<IsStaticGraph G>
const StaticForwardGraph<G>::EdgeList& StaticForwardGraph<G>::get_edges() const
{
    return m_graph.get_edges();
}

template<IsStaticGraph G>
const StaticForwardGraph<G>::EdgeType& StaticForwardGraph<G>::get_edge(EdgeIndex edge) const
{
    return m_graph.get_edge(edge);
}

template<IsStaticGraph G>
size_t StaticForwardGraph<G>::get_num_vertices() const
{
    return m_graph.get_num_vertices();
}

template<IsStaticGraph G>
size_t StaticForwardGraph<G>::get_num_edges() const
{
    return m_graph.get_num_edges();
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
VertexIndex StaticForwardGraph<G>::get_source(EdgeIndex edge) const
{
    return m_graph.template get_source<Direction>(edge);
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
VertexIndex StaticForwardGraph<G>::get_target(EdgeIndex edge) const
{
    return m_graph.template get_target<Direction>(edge);
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
const DegreeList& StaticForwardGraph<G>::get_degrees() const
{
    return m_graph.template get_degrees<Direction>();
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
Degree StaticForwardGraph<G>::get_degree(VertexIndex vertex) const
{
    return m_graph.template get_degree<Direction>(vertex);
}

/* BidirectionalGraph */

template<IsStaticGraph G>
StaticBidirectionalGraph<G>::StaticBidirectionalGraph(G graph) : m_graph(std::move(graph)), m_edge_indices_grouped_by_vertex()
{
    m_edge_indices_grouped_by_vertex.get<ForwardTraversal>() = std::move(compute_index_grouped_edge_indices(m_graph, true));
    m_edge_indices_grouped_by_vertex.get<BackwardTraversal>() = std::move(compute_index_grouped_edge_indices(m_graph, false));
}

template<IsStaticGraph G>
std::ranges::subrange<typename StaticBidirectionalGraph<G>::VertexIndexConstIteratorType> StaticBidirectionalGraph<G>::get_vertex_indices() const
{
    return m_graph.get_vertex_indices();
}

template<IsStaticGraph G>
std::ranges::subrange<typename StaticBidirectionalGraph<G>::EdgeIndexConstIteratorType> StaticBidirectionalGraph<G>::get_edge_indices() const
{
    return m_graph.get_edge_indices();
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
std::ranges::subrange<typename StaticBidirectionalGraph<G>::template AdjacentVertexConstIteratorType<Direction>>
StaticBidirectionalGraph<G>::get_adjacent_vertices(VertexIndex vertex) const
{
    return std::ranges::subrange(
        typename StaticBidirectionalGraph<G>::AdjacentVertexConstIteratorType<Direction>(vertex,
                                                                                         m_graph.get_vertices(),
                                                                                         m_graph.get_edges(),
                                                                                         m_edge_indices_grouped_by_vertex.get<Direction>().at(vertex),
                                                                                         true),
        typename StaticBidirectionalGraph<G>::AdjacentVertexConstIteratorType<Direction>(vertex,
                                                                                         m_graph.get_vertices(),
                                                                                         m_graph.get_edges(),
                                                                                         m_edge_indices_grouped_by_vertex.get<Direction>().at(vertex),
                                                                                         false));
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
std::ranges::subrange<typename StaticBidirectionalGraph<G>::template AdjacentVertexIndexConstIteratorType<Direction>>
StaticBidirectionalGraph<G>::get_adjacent_vertex_indices(VertexIndex vertex) const
{
    return std::ranges::subrange(
        typename StaticBidirectionalGraph<G>::AdjacentVertexIndexConstIteratorType<Direction>(vertex,
                                                                                              m_graph.get_edges(),
                                                                                              m_edge_indices_grouped_by_vertex.get<Direction>().at(vertex),
                                                                                              true),
        typename StaticBidirectionalGraph<G>::AdjacentVertexIndexConstIteratorType<Direction>(vertex,
                                                                                              m_graph.get_edges(),
                                                                                              m_edge_indices_grouped_by_vertex.get<Direction>().at(vertex),
                                                                                              false));
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
std::ranges::subrange<typename StaticBidirectionalGraph<G>::template AdjacentEdgeConstIteratorType<Direction>>
StaticBidirectionalGraph<G>::get_adjacent_edges(VertexIndex vertex) const
{
    return std::ranges::subrange(
        typename StaticBidirectionalGraph<G>::AdjacentEdgeConstIteratorType<Direction>(vertex,
                                                                                       m_graph.get_edges(),
                                                                                       m_edge_indices_grouped_by_vertex.get<Direction>().at(vertex),
                                                                                       true),
        typename StaticBidirectionalGraph<G>::AdjacentEdgeConstIteratorType<Direction>(vertex,
                                                                                       m_graph.get_edges(),
                                                                                       m_edge_indices_grouped_by_vertex.get<Direction>().at(vertex),
                                                                                       false));
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
std::ranges::subrange<typename StaticBidirectionalGraph<G>::template AdjacentEdgeIndexConstIteratorType<Direction>>
StaticBidirectionalGraph<G>::get_adjacent_edge_indices(VertexIndex vertex) const
{
    return std::ranges::subrange(
        typename StaticBidirectionalGraph<G>::AdjacentEdgeIndexConstIteratorType<Direction>(vertex,
                                                                                            m_graph.get_edges(),
                                                                                            m_edge_indices_grouped_by_vertex.get<Direction>().at(vertex),
                                                                                            true),
        typename StaticBidirectionalGraph<G>::AdjacentEdgeIndexConstIteratorType<Direction>(vertex,
                                                                                            m_graph.get_edges(),
                                                                                            m_edge_indices_grouped_by_vertex.get<Direction>().at(vertex),
                                                                                            false));
}

template<IsStaticGraph G>
const StaticBidirectionalGraph<G>::VertexList& StaticBidirectionalGraph<G>::get_vertices() const
{
    return m_graph.get_vertices();
}

template<IsStaticGraph G>
const StaticBidirectionalGraph<G>::VertexType& StaticBidirectionalGraph<G>::get_vertex(VertexIndex vertex) const
{
    return m_graph.get_vertex(vertex);
}

template<IsStaticGraph G>
const StaticBidirectionalGraph<G>::EdgeList& StaticBidirectionalGraph<G>::get_edges() const
{
    return m_graph.get_edges();
}

template<IsStaticGraph G>
const StaticBidirectionalGraph<G>::EdgeType& StaticBidirectionalGraph<G>::get_edge(EdgeIndex edge) const
{
    return m_graph.get_edge(edge);
}

template<IsStaticGraph G>
size_t StaticBidirectionalGraph<G>::get_num_vertices() const
{
    return m_graph.get_num_vertices();
}

template<IsStaticGraph G>
size_t StaticBidirectionalGraph<G>::get_num_edges() const
{
    return m_graph.get_num_edges();
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
VertexIndex StaticBidirectionalGraph<G>::get_source(EdgeIndex edge) const
{
    return m_graph.template get_source<Direction>(edge);
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
VertexIndex StaticBidirectionalGraph<G>::get_target(EdgeIndex edge) const
{
    return m_graph.template get_target<Direction>(edge);
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
const DegreeList& StaticBidirectionalGraph<G>::get_degrees() const
{
    return m_graph.template get_degrees<Direction>();
}

template<IsStaticGraph G>
template<IsTraversalDirection Direction>
Degree StaticBidirectionalGraph<G>::get_degree(VertexIndex vertex) const
{
    return m_graph.template get_degree<Direction>(vertex);
}

}
#endif
