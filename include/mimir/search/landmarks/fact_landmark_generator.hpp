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

#ifndef MIMIR_SEARCH_LANDMARKS_FACT_LANDMARK_GENERATOR_HPP_
#define MIMIR_SEARCH_LANDMARKS_FACT_LANDMARK_GENERATOR_HPP_

#include "mimir/search/declarations.hpp"
#include "mimir/search/grounders/interface.hpp"

namespace mimir::search::landmarks
{

/// @brief `FactLandmarkGeneratorOptions::include_positive_goal_facts = false` yields a graph with
/// an empty landmark set: goal seeding is the only seed source implemented in this version.
struct FactLandmarkGeneratorOptions
{
    bool include_positive_goal_facts = true;
    bool compute_greedy_necessary_orderings = true;
};

/// @brief `ApproximateFactLandmarkGenerator` computes an immutable `FactLandmarkGraph` of
/// approximate positive fluent fact landmarks from the delete-relaxed grounded-action universe of
/// `grounder`, seeded once from the initial state. See `FactLandmarkGraphImpl` for scope notes.
class ApproximateFactLandmarkGenerator
{
public:
    static FactLandmarkGraph create(const IGrounder& grounder, const FactLandmarkGeneratorOptions& options = FactLandmarkGeneratorOptions());
};

}

#endif
