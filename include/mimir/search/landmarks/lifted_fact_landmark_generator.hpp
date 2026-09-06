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

/// @brief How far §9.2's reachability narrowing of an achiever's free variables goes.
enum class ReachabilityDisambiguation
{
    OFF,          ///< No narrowing beyond the static filter.
    PER_LITERAL,  ///< Each precondition literal narrows its own variables against the reachable set.
    JOINT,        ///< The projection of the whole precondition conjunction, never materialised.
                  ///< NOT IMPLEMENTED YET: it needs a projected-query entry point on the engine
                  ///< (§9.2), and `create` throws rather than quietly doing `PER_LITERAL`.
};

/// @brief §9.5: which atoms are tested against the complete Π⁺ fact-landmark characterisation.
enum class CompleteFactLandmarks
{
    OFF,      ///< No completion pass.
    MEMBERS,  ///< Every member of every disjunctive set.
    ALL,      ///< Every reachable non-initial atom. One restricted fixpoint each -- see the docs.
};

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

    /* Delete-relaxed reachability (§9), on the grounding-free `RelaxedReachability` engine. Every
       option below is off-by-construction identical to the pre-0.17 generator when disabled, and
       the byte-identity harness pins that. */

    /// @brief §9.1: drop members that no reachable state holds.
    ///
    /// "Every plan makes some member true" quantifies over reachable states, so an atom outside the
    /// relaxed-reachable set was never one of them. The phase-4 static pre-pass approximates this
    /// syntactically; this is the exact test, and it is what closes the rovers `at(?r, w)` and
    /// logistics cross-city-truck inflation that no static analysis can see (a truck's city is a
    /// fluent initial atom).
    bool reachability_filter_members = true;

    /// @brief §9.2: narrow an achiever's free variables to what the reachable set can supply.
    ///
    /// Defaults to `PER_LITERAL` until `JOINT` exists, so that the default configuration is one the
    /// generator can actually run.
    ReachabilityDisambiguation reachability_disambiguation = ReachabilityDisambiguation::PER_LITERAL;

    /// @brief §9.3: restrict achievers to those that can be the FIRST to add a member.
    ///
    /// Richter-Helmert-Westphal's possible-first-achiever test, computed as one restricted fixpoint
    /// per expansion. Subsumes §2.7 exactly -- and strictly, because it argues about the first
    /// *member* producer where §2.7 could only argue about the first *pattern* producer and had to
    /// gate on pattern instances in `I`. §2.7 is therefore skipped when this is on.
    bool first_achievers_restricted = true;

    /// @brief §9.4: certify every extracted fact against the Π⁺ characterisation, and throw if one
    /// fails. A check of the generator rather than a source of landmarks, on by default so that a
    /// future rule that breaks soundness is caught on the first instance that exercises it.
    bool verify_pi_plus = true;

    /// @brief §9.5: promote atoms that the complete Π⁺ test proves to be landmarks.
    ///
    /// `MEMBERS` is bounded by the member sets. `ALL` is `|R \ I|` restricted fixpoints -- 638,945
    /// of them on sokoban `test/p30-hard`, about 21 hours -- which is why it is the one option that
    /// is opt-in rather than on by default.
    CompleteFactLandmarks complete_fact_landmarks = CompleteFactLandmarks::MEMBERS;
};

/// @brief Whether any option needs the reachability engine built.
inline bool needs_relaxed_reachability(const LiftedFactLandmarkGeneratorOptions& options)
{
    return options.reachability_filter_members || options.reachability_disambiguation != ReachabilityDisambiguation::OFF
           || options.first_achievers_restricted || options.verify_pi_plus
           || options.complete_fact_landmarks != CompleteFactLandmarks::OFF;
}

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
