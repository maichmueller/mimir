/*
 * Copyright (C) 2023 Simon Stahlberg
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

#pragma once

#include "mimir/declarations.hpp"

#include <boost/dynamic_bitset.hpp>
#include <vector>

namespace mimir
{
// Find all cliques of size k in a k-partite graph
void find_all_k_cliques_in_k_partite_graph(const vector<boost::dynamic_bitset<>>& adjacency_matrix,
                                           const vector<vector<size_t>>& partitions,
                                           vector<vector<std::size_t>>& out_cliques);
}
