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

#ifndef MIMIR_GRAPHS_GRAPH_INTERFACE_HPP_
#define MIMIR_GRAPHS_GRAPH_INTERFACE_HPP_

#include "mimir/graphs/graph_edge_interface.hpp"
#include "mimir/graphs/graph_iterators.hpp"

#include <concepts>
#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

namespace mimir
{

template<typename T>
concept IsGraph = requires(T a, VertexIndex vertex, bool forward)
{
    typename T::VertexType;
    requires IsVertex<typename T::VertexType>;
    typename T::EdgeType;
    requires IsEdge<typename T::EdgeType>;

    {
        a.get_adjacent_vertices(vertex, forward)
        } -> std::same_as<VertexIterator<typename T::VertexType, typename T::EdgeType>>;
    {
        a.get_adjacent_vertex_indices(vertex, forward)
        } -> std::same_as<VertexIndexIterator<typename T::EdgeType>>;
    {
        a.get_adjacent_edges(vertex, forward)
        } -> std::same_as<EdgeIterator<typename T::EdgeType>>;
    {
        a.get_adjacent_edge_indices(vertex, forward)
        } -> std::same_as<EdgeIndexIterator<typename T::EdgeType>>;
    {
        a.get_vertices()
        } -> std::same_as<const std::vector<typename T::VertexType>&>;
    {
        a.get_edges()
        } -> std::same_as<const std::vector<typename T::EdgeType>&>;
    {
        a.get_num_vertices()
        } -> std::same_as<size_t>;
    {
        a.get_num_edges()
        } -> std::same_as<size_t>;
};

template<typename T>
concept IsConstructibleGraph = requires(T a, VertexIndex vertex)
{
    {
        a.add_vertex()
        } -> std::same_as<VertexIndex>;
    {
        a.add_directed_edge(vertex, vertex)
        } -> std::same_as<EdgeIndex>;
    {
        a.add_undirected_edge(vertex, vertex)
        } -> std::same_as<std::pair<EdgeIndex, EdgeIndex>>;
};

}

#endif
