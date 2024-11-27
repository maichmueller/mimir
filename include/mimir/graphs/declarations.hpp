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

#pragma once

// Do not include headers with transitive dependencies.
#include "mimir/common/types.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mimir
{

using VertexIndex = Index;
using VertexIndexList = vector<VertexIndex>;
using VertexIndexSet = unordered_set<VertexIndex>;

using EdgeIndex = Index;
using EdgeIndexList = vector<EdgeIndex>;
using EdgeIndexSet = unordered_set<EdgeIndex>;

using Degree = uint32_t;
using DegreeList = vector<Degree>;
using DegreeMap = unordered_map<VertexIndex, Degree>;

using Color = uint32_t;
using ColorList = vector<Color>;
template<size_t K>
using ColorArray = std::array<Color, K>;
using ColorSet = unordered_set<Color>;
template<typename T>
using ColorMap = unordered_map<Color, T>;

}
