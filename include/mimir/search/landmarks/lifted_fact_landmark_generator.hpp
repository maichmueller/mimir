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

#ifndef MIMIR_SEARCH_LANDMARKS_LIFTED_FACT_LANDMARK_GENERATOR_HPP_
#define MIMIR_SEARCH_LANDMARKS_LIFTED_FACT_LANDMARK_GENERATOR_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/search/declarations.hpp"

#include <cstddef>

namespace mimir::search::landmarks
{

/// @brief Options for `LiftedFactLandmarkGenerator`.
struct LiftedFactLandmarkGeneratorOptions
{
    /// @brief Goal seeding is the only seed source, so `false` yields an empty landmark set --
    /// parity with `FactLandmarkGeneratorOptions::include_positive_goal_facts`.
    bool include_positive_goal_facts = true;

    /// @brief Record `->_D` ("ordered directly before") edges. Only edges whose two endpoints are
    /// both *fact* landmarks reach the graph's atom-indexed vectors; an edge touching a partial
    /// landmark has no atom index to be stored under and lives in `LiftedLandmark::parent_positions`.
    bool compute_greedy_necessary_orderings = true;

    /// @brief Drop an achiever with no statically consistent instance, and bind a variable that
    /// every statically consistent instance binds the same way.
    ///
    /// Static atoms never change, so an achiever whose static preconditions cannot be met has no
    /// applicable ground instance at all and cannot be the first achiever of anything. Dropping it
    /// *sharpens* the intersection over achievers, which is where childsnack's precision comes
    /// from: with the goal binding `?c`, exactly one of `serve_sandwich`/`serve_sandwich_no_gluten`
    /// survives `allergic_gluten(?c)`/`not_allergic_gluten(?c)`, and `waiting(?c, ?p)` then binds
    /// the place. Switching it off is sound but coarse -- the intersection has to hold over
    /// achievers that can never fire.
    bool use_static_filter = true;

    /// @brief Cap on the number of choice vectors enumerated per predicate, or 0/1 for the
    /// first-occurrence vector only.
    ///
    /// When an achiever's precondition mentions a predicate `Q` several times, *every* way of
    /// picking one occurrence per achiever yields its own sound landmark, and they genuinely differ
    /// (footnote 1 of Wichlacz et al., IJCAI 2022): achiever 1 with `Q(a,x), Q(y,b)` against
    /// achiever 2 with `Q(a,b)` gives both `Q(a,_)` and `Q(_,b)`. The product of the occurrence
    /// counts is what this bounds.
    size_t max_occurrence_combinations = 64;

    /// @brief Largest disjunctive member set to keep, or 0 for UNCAPPED.
    ///
    /// Note the inverted meaning against `FactLandmarkGeneratorOptions::max_disjunctive_landmark_size`,
    /// where 0 switches the feature off: here a partial landmark *is* the feature, so switching it
    /// off is not a thing this generator can do, and 0 is the natural "no bound". A set over the cap
    /// is dropped from the graph's disjunctive landmarks, never truncated (mimir convention -- a
    /// truncated set is not a landmark). The landmark's own `LiftedLandmark` record keeps its full
    /// member list either way, because that record is diagnostics, not vocabulary.
    size_t max_disjunctive_members = 0;

    /// @brief Turn a partial landmark with exactly one member into a fact landmark.
    ///
    /// Every plan makes *some* member true, and there is exactly one, so that member is mandatory.
    /// Promoting it is what lets the back-chain continue through it as a fact rather than stopping
    /// at a one-element disjunction.
    bool promote_singleton_disjunctions = true;
};

/// @brief `LiftedFactLandmarkGenerator` computes necessary-subgoal landmarks over *partially ground
/// atoms*, directly from the action schemas -- no ground action is ever instantiated.
///
/// This is Wichlacz, Höller & Hoffmann, *Landmark Heuristics for Lifted Classical Planning*,
/// IJCAI 2022, §3.1 ("Necessary Subgoals", Proposition 1). Back-chaining from a subgoal `P(u)`, it
/// collects the schema-level achievers of `P(u)`, and for every fluent predicate `Q` that *every*
/// achiever's precondition mentions it emits the partially ground atom on which those occurrences
/// agree position by position. Every plan fires one of the achievers and every achiever needs one
/// of those atoms, so the resulting set is a landmark even where no single atom is.
///
/// Against `ApproximateFactLandmarkGenerator` the trade is precision for scale. The grounded
/// generator must instantiate the delete-relaxed ground-action universe before it can back-chain
/// (37 min / 23.4 GB on childsnack `p30-hard`), and in exchange it can intersect over the actually
/// reachable achievers; this one reads the schemas and is milliseconds, but its member sets are a
/// superset of the reachable ones. It also does not inherit the grounded generator's
/// h_max-minimal-achiever approximation, which reports initially-true atoms (blocksworld
/// `ontable(A)`) as landmarks they are not.
class LiftedFactLandmarkGenerator
{
public:
    static FactLandmarkGraph create(const formalism::Problem& problem,
                                    const LiftedFactLandmarkGeneratorOptions& options = LiftedFactLandmarkGeneratorOptions());
};

}

#endif
