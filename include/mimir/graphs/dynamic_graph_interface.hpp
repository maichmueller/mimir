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

#ifndef MIMIR_GRAPHS_DYNAMIC_GRAPH_INTERFACE_HPP_
#define MIMIR_GRAPHS_DYNAMIC_GRAPH_INTERFACE_HPP_

#include "mimir/graphs/graph_interface.hpp"

namespace mimir
{

/**
 * Dynamic graphs allow for deletion of vertices and edges, requiring hash map data structures.
 */

struct DynamicGraphTag
{
};

template<typename T>
concept IsDynamicGraph = requires(T a)
{
    typename T::GraphType;
    requires std::same_as<typename T::GraphType, DynamicGraphTag>;

    requires IsVertexListGraph<T>;
    requires IsEdgeListGraph<T>;
    requires IsIncidenceGraph<T>;
    requires IsAdjacencyGraph<T>;
};

}

#endif
