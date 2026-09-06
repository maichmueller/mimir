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

#ifndef MIMIR_SEARCH_RELAXED_REACHABILITY_HPP_
#define MIMIR_SEARCH_RELAXED_REACHABILITY_HPP_

#include "mimir/formalism/declarations.hpp"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace mimir::search
{

/// @brief Internals of the atom-level Datalog engine. Declared here only so that the public types
/// can hold pointers to them; the definitions live in `src/search/relaxed_reachability.cpp`.
namespace relaxed_reachability
{
class Relation;
class Program;
class Database;
}

/**
 * Options
 */

struct RelaxedReachabilityOptions
{
    /// @brief Evaluate negative static preconditions (including `(not (= ?x ?y))`) as filters.
    ///
    /// This is the exact reading of the delete relaxation: deleting *fluent* effects does not make a
    /// negative *static* precondition go away, because static atoms are never touched by any action.
    /// mimir's `DeleteRelaxTranslator` drops every negative literal regardless of tag
    /// (`src/formalism/translator/delete_relax.cpp`, `filter_positive_literals`), so `LiftedGrounder`'s
    /// delete-free exploration can reach atoms through actions that no real ground action can execute.
    /// Setting this to false reproduces that over-approximation, which is what the exactness test uses
    /// to explain any difference between the two reachable sets.
    bool enforce_negative_static_conditions = true;

};

/**
 * Statistics
 */

struct RelaxedReachabilityStatistics
{
    size_t num_rules = 0;                    ///< Datalog rules before splitting (one per schema/effect/add-literal, plus one per axiom).
    size_t num_dropped_rules = 0;            ///< Rules a statically unsatisfiable condition removed at compile time.
    size_t num_join_steps = 0;               ///< Binary joins after splitting, summed over all rules.
    size_t num_relations = 0;                ///< Predicate relations plus type domains plus auxiliaries.
    size_t num_auxiliary_relations = 0;      ///< Of those, the intermediates rule splitting introduced.
    size_t num_fixpoint_rounds = 0;          ///< Semi-naive rounds of the unrestricted fixpoint.
    size_t num_reachable_fluent_atoms = 0;   ///< Relaxed-reachable ground atoms over fluent predicates.
    size_t num_reachable_derived_atoms = 0;  ///< Relaxed-reachable ground atoms over derived predicates.
    size_t num_auxiliary_tuples = 0;         ///< Tuples materialised in auxiliary relations (the memory the splitting costs).
    size_t num_static_tuples = 0;            ///< Tuples in the static EDB relations (shared by every query).
    double compile_time_ms = 0.0;   ///< Reading the schemas, splitting the rules and choosing the join orders.
    double fixpoint_time_ms = 0.0;  ///< The unrestricted fixpoint, excluding compilation.
};

/**
 * ReachableTuples
 */

/// @brief A read-only view of the reachable tuples of one predicate.
///
/// The engine never interns a ground atom: it hands out object tuples and lets the caller decide what
/// to put into the problem's repositories. A view is valid for as long as the table it came from.
class ReachableTuples
{
private:
    const relaxed_reachability::Relation* m_relation;  ///< nullptr when the predicate has no relation at all.
    const formalism::ObjectList* m_objects_by_id;

public:
    ReachableTuples(const relaxed_reachability::Relation* relation, const formalism::ObjectList* objects_by_id);

    size_t get_arity() const;
    size_t size() const;
    bool empty() const;

    /// @brief The `position`-th tuple, materialised as objects.
    formalism::ObjectList operator[](size_t position) const;

    /// @brief Append the `position`-th tuple to `out` without allocating a fresh list.
    void write(size_t position, formalism::ObjectList& out) const;
};

/**
 * ReachabilityTable
 */

/// @brief The atoms one delete-relaxed fixpoint reached.
///
/// Returned by `RelaxedReachability::compute_restricted`. Owns the derived relations; the rule plan and
/// the static relations are shared with the `RelaxedReachability` that produced it, which must outlive it.
class ReachabilityTable
{
private:
    std::shared_ptr<const relaxed_reachability::Program> m_program;
    std::unique_ptr<relaxed_reachability::Database> m_database;

public:
    ReachabilityTable(std::shared_ptr<const relaxed_reachability::Program> program, std::unique_ptr<relaxed_reachability::Database> database);
    ~ReachabilityTable();

    ReachabilityTable(const ReachabilityTable& other) = delete;
    ReachabilityTable& operator=(const ReachabilityTable& other) = delete;
    ReachabilityTable(ReachabilityTable&& other) noexcept;
    ReachabilityTable& operator=(ReachabilityTable&& other) noexcept;

    bool is_reachable(formalism::Predicate<formalism::FluentTag> predicate, const formalism::ObjectList& objects) const;
    bool is_reachable(formalism::Predicate<formalism::DerivedTag> predicate, const formalism::ObjectList& objects) const;
    bool is_reachable(formalism::GroundAtom<formalism::FluentTag> atom) const;
    bool is_reachable(formalism::GroundAtom<formalism::DerivedTag> atom) const;

    ReachableTuples get_reachable_tuples(formalism::Predicate<formalism::FluentTag> predicate) const;
    ReachableTuples get_reachable_tuples(formalism::Predicate<formalism::DerivedTag> predicate) const;

    /// @brief Reachable fluent plus derived ground atoms.
    size_t get_num_reachable_atoms() const;
    size_t get_num_reachable_fluent_atoms() const;
    size_t get_num_reachable_derived_atoms() const;

    /// @brief Are all positive fluent and derived goal atoms reachable, and does the static goal hold?
    bool is_goal_reachable() const;

    size_t get_num_fixpoint_rounds() const;
};

/**
 * RelaxedReachability
 */

/// @brief Exact delete-relaxed reachability over ground ATOMS, without ever enumerating a ground action.
///
/// WHY: `LiftedGrounder` computes the same fixpoint by instantiating every applicable ground action of a
/// delete-free copy of the problem. On childsnack `test/p30-hard` that is ~37 million `make_sandwich`
/// instances (292 breads x 292 contents x 437 sandwiches) for a reachable atom set of a few thousand
/// atoms. Here the schemas are read as Datalog rules over object tuples, each rule is split into a chain
/// of binary joins that projects away every variable it no longer needs, and the fixpoint is evaluated
/// semi-naively. `make_sandwich` then costs |breads| + |contents| + |sandwiches| probes rather than their
/// product, because `?b` and `?c` do not occur in the head and die at their own join.
///
/// SCOPE (delete relaxation). Negative *fluent* and *derived* preconditions are ignored: an atom, once
/// derived, is never retracted, so a negative condition over them cannot be evaluated in the relaxation.
/// That over-approximates reachability, which is the sound direction for every consumer (a landmark test
/// that says "the goal is still reachable" must not be wrong in the direction that invents landmarks).
/// Numeric constraints and numeric effects are ignored for the same reason. Negative *static*
/// preconditions are evaluated exactly, see `RelaxedReachabilityOptions::enforce_negative_static_conditions`.
///
/// Axioms become rules whose head is a derived atom, so `DerivedTag` predicates are part of the fixpoint.
/// No stratification is required: the fixpoint is monotone because negation is only over the static EDB.
class RelaxedReachability
{
public:
    /// @brief A ground atom to forbid, as predicate plus objects, so a caller can name an atom that has
    /// never been interned into the problem's repositories.
    using ForbiddenAtom = std::pair<formalism::Predicate<formalism::FluentTag>, formalism::ObjectList>;
    using ForbiddenAtomList = std::vector<ForbiddenAtom>;

private:
    formalism::Problem m_problem;
    RelaxedReachabilityOptions m_options;
    std::shared_ptr<const relaxed_reachability::Program> m_program;
    std::unique_ptr<ReachabilityTable> m_table;
    RelaxedReachabilityStatistics m_statistics;

    RelaxedReachability(formalism::Problem problem, RelaxedReachabilityOptions options);

public:
    ~RelaxedReachability();
    RelaxedReachability(const RelaxedReachability& other) = delete;
    RelaxedReachability& operator=(const RelaxedReachability& other) = delete;
    RelaxedReachability(RelaxedReachability&& other) = delete;
    RelaxedReachability& operator=(RelaxedReachability&& other) = delete;

    /// @brief Compile the rule plan for `problem` and run the unrestricted fixpoint once.
    static std::shared_ptr<const RelaxedReachability> create(const formalism::Problem& problem, const RelaxedReachabilityOptions& options = {});

    /* Unrestricted reachability. */

    bool is_reachable(formalism::Predicate<formalism::FluentTag> predicate, const formalism::ObjectList& objects) const;
    bool is_reachable(formalism::Predicate<formalism::DerivedTag> predicate, const formalism::ObjectList& objects) const;
    bool is_reachable(formalism::GroundAtom<formalism::FluentTag> atom) const;
    bool is_reachable(formalism::GroundAtom<formalism::DerivedTag> atom) const;

    ReachableTuples get_reachable_tuples(formalism::Predicate<formalism::FluentTag> predicate) const;
    ReachableTuples get_reachable_tuples(formalism::Predicate<formalism::DerivedTag> predicate) const;

    size_t get_num_reachable_atoms() const;
    bool is_goal_reachable() const;

    /// @brief The unrestricted table, for callers that want to hold on to it.
    const ReachabilityTable& get_table() const;

    /* Restricted reachability (Richter-Helmert-Westphal "possible first achievers"). */

    /// @brief Re-run the fixpoint with `forbidden` removed from the initial state and every derivation
    /// whose head is one of those atoms dropped. Reuses the compiled plan and the static relations.
    ReachabilityTable compute_restricted(const ForbiddenAtomList& forbidden) const;
    ReachabilityTable compute_restricted(const formalism::GroundAtomList<formalism::FluentTag>& forbidden) const;

    /// @brief The complete delete-relaxation landmark test: is the goal still reachable without `forbidden`?
    /// Stops as soon as the goal is reached, so it is cheaper than `compute_restricted` on a solvable query.
    bool is_goal_reachable_without(const ForbiddenAtomList& forbidden) const;
    bool is_goal_reachable_without(const formalism::GroundAtomList<formalism::FluentTag>& forbidden) const;

    const formalism::Problem& get_problem() const;
    const RelaxedReachabilityOptions& get_options() const;
    const RelaxedReachabilityStatistics& get_statistics() const;
};

using RelaxedReachabilityPtr = std::shared_ptr<const RelaxedReachability>;

}

#endif
