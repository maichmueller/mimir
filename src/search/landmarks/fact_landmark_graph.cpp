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

#include "mimir/search/landmarks/fact_landmark_graph.hpp"

#include "mimir/formalism/ground_action.hpp"
#include "mimir/formalism/ground_atom.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/formalism/repositories.hpp"
#include "mimir/search/state.hpp"

#include <algorithm>

using namespace mimir::formalism;

namespace mimir::search::landmarks
{

FactLandmarkGraphImpl::FactLandmarkGraphImpl(formalism::Problem problem,
                                             FlatBitset landmark_atom_mask,
                                             IndexList landmark_atom_indices,
                                             std::vector<IndexList> disjunctive_landmarks,
                                             std::vector<IndexList> achiever_action_indices_by_atom,
                                             std::vector<IndexList> first_achiever_action_indices_by_atom,
                                             std::vector<IndexList> landmarks_achieved_by_action,
                                             std::vector<IndexList> landmarks_first_achieved_by_action,
                                             std::vector<IndexList> landmarks_uniquely_achieved_by_action,
                                             std::vector<IndexList> predecessors_by_atom,
                                             std::vector<IndexList> successors_by_atom) :
    m_problem(std::move(problem)),
    m_landmark_atom_mask(std::move(landmark_atom_mask)),
    m_landmark_atom_indices(std::move(landmark_atom_indices)),
    m_disjunctive_landmarks(std::move(disjunctive_landmarks)),
    m_achiever_action_indices_by_atom(std::move(achiever_action_indices_by_atom)),
    m_first_achiever_action_indices_by_atom(std::move(first_achiever_action_indices_by_atom)),
    m_landmarks_achieved_by_action(std::move(landmarks_achieved_by_action)),
    m_landmarks_first_achieved_by_action(std::move(landmarks_first_achieved_by_action)),
    m_landmarks_uniquely_achieved_by_action(std::move(landmarks_uniquely_achieved_by_action)),
    m_predecessors_by_atom(std::move(predecessors_by_atom)),
    m_successors_by_atom(std::move(successors_by_atom))
{
}

const formalism::Problem& FactLandmarkGraphImpl::get_problem() const { return m_problem; }

const IndexList& FactLandmarkGraphImpl::get_landmark_atom_indices() const { return m_landmark_atom_indices; }

GroundAtomList<FluentTag> FactLandmarkGraphImpl::get_landmark_atoms() const
{
    auto result = GroundAtomList<FluentTag> {};
    result.reserve(m_landmark_atom_indices.size());
    for (const auto atom_index : m_landmark_atom_indices)
    {
        result.push_back(m_problem->get_repositories().get_ground_atom<FluentTag>(atom_index));
    }
    return result;
}

const std::vector<IndexList>& FactLandmarkGraphImpl::get_disjunctive_landmarks() const { return m_disjunctive_landmarks; }

IndexList FactLandmarkGraphImpl::get_disjunctive_landmark_atom_indices() const
{
    auto result = IndexList {};
    for (const auto& members : m_disjunctive_landmarks)
    {
        result.insert(result.end(), members.begin(), members.end());
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

bool FactLandmarkGraphImpl::is_landmark(Index atom_index) const { return m_landmark_atom_mask.get(atom_index); }

bool FactLandmarkGraphImpl::is_landmark(GroundAtom<FluentTag> atom) const { return is_landmark(atom->get_index()); }

IndexList FactLandmarkGraphImpl::get_achieved_landmark_atom_indices(const State& state) const
{
    auto result = IndexList {};
    const auto& state_atoms = state.get_atoms<FluentTag>();
    for (const auto atom_index : m_landmark_atom_indices)
    {
        if (state_atoms.get(atom_index))
        {
            result.push_back(atom_index);
        }
    }
    return result;
}

IndexList FactLandmarkGraphImpl::get_unachieved_landmark_atom_indices(const State& state) const
{
    auto result = IndexList {};
    const auto& state_atoms = state.get_atoms<FluentTag>();
    for (const auto atom_index : m_landmark_atom_indices)
    {
        if (!state_atoms.get(atom_index))
        {
            result.push_back(atom_index);
        }
    }
    return result;
}

const IndexList& FactLandmarkGraphImpl::get_achiever_action_indices(Index landmark_atom_index) const
{
    return m_achiever_action_indices_by_atom[landmark_atom_index];
}

GroundActionList FactLandmarkGraphImpl::get_achievers(Index landmark_atom_index) const
{
    auto result = GroundActionList {};
    const auto& indices = get_achiever_action_indices(landmark_atom_index);
    result.reserve(indices.size());
    for (const auto action_index : indices)
    {
        result.push_back(get_ground_action(action_index));
    }
    return result;
}

const IndexList& FactLandmarkGraphImpl::get_first_achiever_action_indices(Index landmark_atom_index) const
{
    return m_first_achiever_action_indices_by_atom[landmark_atom_index];
}

GroundActionList FactLandmarkGraphImpl::get_first_achievers(Index landmark_atom_index) const
{
    auto result = GroundActionList {};
    const auto& indices = get_first_achiever_action_indices(landmark_atom_index);
    result.reserve(indices.size());
    for (const auto action_index : indices)
    {
        result.push_back(get_ground_action(action_index));
    }
    return result;
}

std::optional<Index> FactLandmarkGraphImpl::get_unique_achiever_action_index(Index landmark_atom_index) const
{
    const auto& indices = get_achiever_action_indices(landmark_atom_index);
    return (indices.size() == 1) ? std::make_optional(indices.front()) : std::nullopt;
}

std::optional<GroundAction> FactLandmarkGraphImpl::get_unique_achiever(Index landmark_atom_index) const
{
    const auto action_index = get_unique_achiever_action_index(landmark_atom_index);
    return action_index.has_value() ? std::make_optional(get_ground_action(*action_index)) : std::nullopt;
}

bool FactLandmarkGraphImpl::is_landmark_achiever(GroundAction action) const
{
    return !get_landmarks_achieved_by_action(action).empty();
}

bool FactLandmarkGraphImpl::is_first_landmark_achiever(GroundAction action) const
{
    return !m_landmarks_first_achieved_by_action[action->get_index()].empty();
}

bool FactLandmarkGraphImpl::is_unique_landmark_achiever(GroundAction action) const
{
    return !get_landmarks_uniquely_achieved_by_action(action).empty();
}

const IndexList& FactLandmarkGraphImpl::get_landmarks_achieved_by_action(GroundAction action) const
{
    return m_landmarks_achieved_by_action[action->get_index()];
}

const IndexList& FactLandmarkGraphImpl::get_landmarks_uniquely_achieved_by_action(GroundAction action) const
{
    return m_landmarks_uniquely_achieved_by_action[action->get_index()];
}

const IndexList& FactLandmarkGraphImpl::get_predecessors(Index landmark_atom_index) const { return m_predecessors_by_atom[landmark_atom_index]; }

const IndexList& FactLandmarkGraphImpl::get_successors(Index landmark_atom_index) const { return m_successors_by_atom[landmark_atom_index]; }

GroundAction FactLandmarkGraphImpl::get_ground_action(Index action_index) const
{
    const auto& ground_action_repository = boost::hana::at_key(m_problem->get_repositories().get_hana_repositories(), boost::hana::type<GroundActionImpl> {});
    return ground_action_repository.at(action_index);
}

}
