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

#ifndef MIMIR_SEARCH_LANDMARKS_FACT_LANDMARK_GRAPH_HPP_
#define MIMIR_SEARCH_LANDMARKS_FACT_LANDMARK_GRAPH_HPP_

#include "mimir/common/declarations.hpp"
#include "mimir/common/types_cista.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/search/declarations.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mimir::search::landmarks
{

/// @brief `LiftedLandmark` is the *intensional* form of a landmark: a partially ground atom
/// `Q(w1...wn)` where a bound position carries an object and a free position carries `nullptr`.
///
/// It exists because the lifted extractor derives landmarks over patterns, not over ground atoms,
/// and that pattern is the only place the derivation is legible: `ontray(?, ?)` is one landmark
/// with hundreds of members, and printing its member list says nothing about why it was derived.
/// The graph's atom-indexed vectors cannot hold a partial landmark at all (it has no atom index),
/// so orderings that touch one live here in `parent_positions` and nowhere else.
struct LiftedLandmark
{
    formalism::Predicate<formalism::FluentTag> predicate;

    /// @brief One entry per predicate position; `nullptr` marks a free position.
    formalism::ObjectList binding;

    /// @brief The ground vocabulary of this landmark: every plan makes one of these true.
    /// A fully bound landmark's member list is exactly its own atom.
    IndexList member_atom_indices;

    /// @brief Set iff the landmark is fully bound, in which case it is a *fact* landmark.
    std::optional<Index> fact_atom_index;

    /// @brief Positions in the enclosing `std::vector<LiftedLandmark>` of the landmarks this one
    /// was back-chained from, i.e. `this ->_D parent` ("ordered directly before").
    IndexList parent_positions;

    /// @brief Some instance of this landmark holds in the initial state, so it was recorded but
    /// never expanded -- see `LiftedFactLandmarkGeneratorOptions` for why expanding it is unsound.
    bool initially_true = false;
};

/// @brief Render a lifted landmark as `predicate(arg, ?, ...)`, with `?` for a free position.
extern std::string to_string(const LiftedLandmark& landmark);

extern std::ostream& operator<<(std::ostream& out, const LiftedLandmark& landmark);

/// @brief `FactLandmarkGraphImpl` stores an immutable set of approximate positive fluent fact
/// landmarks for a problem, computed once by `ApproximateFactLandmarkGeneratorImpl`, together with
/// their grounded achievers and greedy-necessary ordering edges. Scope is restricted to positive
/// fluent atoms: derived/axiom atoms and negative preconditions are not considered.
class FactLandmarkGraphImpl
{
public:
    FactLandmarkGraphImpl(formalism::Problem problem,
                           FlatBitset landmark_atom_mask,
                           IndexList landmark_atom_indices,
                           std::vector<IndexList> disjunctive_landmarks,
                           std::vector<IndexList> achiever_action_indices_by_atom,
                           std::vector<IndexList> first_achiever_action_indices_by_atom,
                           std::vector<IndexList> landmarks_achieved_by_action,
                           std::vector<IndexList> landmarks_first_achieved_by_action,
                           std::vector<IndexList> landmarks_uniquely_achieved_by_action,
                           std::vector<IndexList> predecessors_by_atom,
                           std::vector<IndexList> successors_by_atom);

    /// @brief The achiever-free form: no ground action was ever instantiated, so every
    /// action-indexed accessor is absent rather than empty. See `has_achiever_index`.
    FactLandmarkGraphImpl(formalism::Problem problem,
                          FlatBitset landmark_atom_mask,
                          IndexList landmark_atom_indices,
                          std::vector<IndexList> disjunctive_landmarks,
                          std::vector<IndexList> predecessors_by_atom,
                          std::vector<IndexList> successors_by_atom,
                          std::vector<LiftedLandmark> lifted_landmarks);

    /// @brief Build a graph from atom indices alone -- no grounder, no achiever index.
    ///
    /// Normalises what the caller hands over: each disjunctive set is sorted and deduplicated,
    /// empty sets, sets holding a fact landmark (subsumed by it) and exact duplicates are dropped,
    /// and the per-atom ordering vectors are sized to the largest atom index mentioned.
    static FactLandmarkGraph create(formalism::Problem problem,
                                    IndexList landmark_atom_indices,
                                    std::vector<IndexList> disjunctive_landmarks,
                                    std::vector<IndexList> predecessors_by_atom = {},
                                    std::vector<IndexList> successors_by_atom = {},
                                    std::vector<LiftedLandmark> lifted_landmarks = {});

    const formalism::Problem& get_problem() const;

    /// @brief Whether the graph carries the per-ground-action achiever index.
    ///
    /// False for a graph built without grounding (`LiftedFactLandmarkGenerator`, `create`): the
    /// achiever accessors then throw rather than returning empty lists, because "this action
    /// achieves no landmark" and "this graph cannot answer that" are different facts and a
    /// consumer that silently reads the first for the second is silently wrong.
    bool has_achiever_index() const;

    /// @brief The intensional landmarks, in discovery order, or empty for a grounded graph.
    const std::vector<LiftedLandmark>& get_lifted_landmarks() const;

    const IndexList& get_landmark_atom_indices() const;
    formalism::GroundAtomList<formalism::FluentTag> get_landmark_atoms() const;

    /// @brief The disjunctive landmarks: sets of which every plan makes at least one member true.
    ///
    /// Empty unless `FactLandmarkGeneratorOptions::max_disjunctive_landmark_size` was set. Each set
    /// is ascending and deduplicated, no set contains a fact landmark (those are subsumed), and no
    /// two sets are equal.
    ///
    /// Deliberately *not* folded into `get_landmark_atom_indices()`, and the reason is the whole
    /// point of the split: a member is not individually mandatory, so the two sets answer different
    /// questions and have different consumers. LIW's novelty coordinate wants the members ranked
    /// (see `iw::LandmarkCoordinates`); anything reading landmarks as obligations must not see
    /// them. Folding them together would make those two settings one flag and their effects
    /// impossible to attribute.
    const std::vector<IndexList>& get_disjunctive_landmarks() const;

    /// @brief Every atom appearing in some disjunctive landmark, ascending and deduplicated.
    IndexList get_disjunctive_landmark_atom_indices() const;

    bool is_landmark(Index atom_index) const;
    bool is_landmark(formalism::GroundAtom<formalism::FluentTag> atom) const;

    IndexList get_achieved_landmark_atom_indices(const State& state) const;
    IndexList get_unachieved_landmark_atom_indices(const State& state) const;

    const IndexList& get_achiever_action_indices(Index landmark_atom_index) const;
    formalism::GroundActionList get_achievers(Index landmark_atom_index) const;

    const IndexList& get_first_achiever_action_indices(Index landmark_atom_index) const;
    formalism::GroundActionList get_first_achievers(Index landmark_atom_index) const;

    std::optional<Index> get_unique_achiever_action_index(Index landmark_atom_index) const;
    std::optional<formalism::GroundAction> get_unique_achiever(Index landmark_atom_index) const;

    bool is_landmark_achiever(formalism::GroundAction action) const;
    bool is_first_landmark_achiever(formalism::GroundAction action) const;
    bool is_unique_landmark_achiever(formalism::GroundAction action) const;

    const IndexList& get_landmarks_achieved_by_action(formalism::GroundAction action) const;
    const IndexList& get_landmarks_uniquely_achieved_by_action(formalism::GroundAction action) const;

    /// @brief Greedy-necessary predecessors of a fact landmark, empty beyond the stored range.
    ///
    /// The bounds check is not defensive padding: a lifted problem interns fluent atoms during
    /// search, so the atom universe keeps growing after the graph was built and a perfectly
    /// legitimate atom index can exceed what the graph was sized for.
    const IndexList& get_predecessors(Index landmark_atom_index) const;
    const IndexList& get_successors(Index landmark_atom_index) const;

private:
    formalism::Problem m_problem;

    FlatBitset m_landmark_atom_mask;
    IndexList m_landmark_atom_indices;
    std::vector<IndexList> m_disjunctive_landmarks;

    std::vector<IndexList> m_achiever_action_indices_by_atom;
    std::vector<IndexList> m_first_achiever_action_indices_by_atom;

    std::vector<IndexList> m_landmarks_achieved_by_action;
    std::vector<IndexList> m_landmarks_first_achieved_by_action;
    std::vector<IndexList> m_landmarks_uniquely_achieved_by_action;

    std::vector<IndexList> m_predecessors_by_atom;
    std::vector<IndexList> m_successors_by_atom;

    bool m_has_achiever_index;
    std::vector<LiftedLandmark> m_lifted_landmarks;

    formalism::GroundAction get_ground_action(Index action_index) const;

    /// @brief Throws unless the achiever index is present.
    void assert_achiever_index() const;
};

}

#endif
