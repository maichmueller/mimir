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

    /// @brief Largest disjunctive landmark to keep, or 0 to not compute them at all (the default,
    /// which leaves the graph byte-identical to one built before disjunctive landmarks existed).
    ///
    /// A *fact* landmark survives the back-chain only when every minimal-cost achiever of its
    /// parent shares the precondition, i.e. when the intersection over achievers is non-empty. With
    /// several achievers that intersection is routinely empty and the chain stops -- even though
    /// the achievers may still agree on a *predicate*, each contributing a different ground atom of
    /// it. That agreement is a landmark of the set: every plan fires some achiever, and every
    /// achiever needs one of those atoms. This option recovers exactly that, with the union bounded
    /// by `max_disjunctive_landmark_size` so a wide disjunction is dropped rather than tracked.
    ///
    /// Sets containing a fact landmark are discarded as subsumed: that member holds in every plan,
    /// so the set says nothing the fact landmark does not already say.
    size_t max_disjunctive_landmark_size = 0;

    /// @brief How many back-chaining rounds to take through disjunctive landmarks, or 0 for as many
    /// as terminate. Ignored when `max_disjunctive_landmark_size` is 0.
    ///
    /// Unlike fact landmarks, the members of a disjunctive landmark are not themselves landmarks,
    /// so back-chaining through them searches for stepping stones rather than deriving them -- each
    /// round multiplies the frontier by the achiever count. Termination is guaranteed regardless
    /// (an atom is expanded at most once), so this bounds breadth, not correctness.
    size_t max_disjunctive_landmark_depth = 0;
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
