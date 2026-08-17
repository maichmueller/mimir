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
#include <vector>

namespace mimir::search::landmarks
{

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

    const formalism::Problem& get_problem() const;

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

    formalism::GroundAction get_ground_action(Index action_index) const;
};

}

#endif
