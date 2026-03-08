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

#include "mimir/search/algorithms/iw/tuple_index_mapper.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace mimir::search::iw
{
TupleIndexMapper::TupleIndexMapper(size_t arity) : TupleIndexMapper(arity, 0) {}

TupleIndexMapper::TupleIndexMapper(size_t arity, size_t num_atoms) : m_arity(arity), m_num_atoms(num_atoms), m_empty_tuple_index(0)
{
    initialize(arity, num_atoms);
}

void TupleIndexMapper::initialize(size_t arity, size_t num_atoms)
{
    m_arity = arity;
    m_num_atoms = num_atoms;
    m_empty_tuple_index = 0;

    if (!(arity >= 0 && arity < MAX_ARITY))
    {
        throw std::runtime_error("TupleIndexMapper only works with 0 <= arity < " + std::to_string(MAX_ARITY) + ".");
    }

    for (size_t i = 0; i < m_arity; ++i)
    {
        m_factors[i] = std::pow((m_num_atoms + 1), i);
    }
}

TupleIndex TupleIndexMapper::to_tuple_index(const AtomIndexList& atom_indices) const
{
    assert(std::is_sorted(atom_indices.begin(), atom_indices.end()));
    assert(atom_indices.size() <= m_arity);

    TupleIndex result = 0;
    for (size_t i = 0; i < atom_indices.size(); ++i)
    {
        result += m_factors[i] * atom_indices[i];
    }
    for (size_t i = atom_indices.size(); i < m_arity; ++i)
    {
        result += m_factors[i] * m_num_atoms;
    }
    return result;
}

void TupleIndexMapper::to_atom_indices(TupleIndex tuple_index, AtomIndexList& out_atom_indices) const
{
    out_atom_indices.clear();

    for (int i = m_arity - 1; i >= 0; --i)
    {
        const auto atom_index = tuple_index / m_factors[i];

        if (atom_index != m_num_atoms)
        {
            out_atom_indices.push_back(atom_index);
        }

        tuple_index -= atom_index * m_factors[i];
    }
    std::reverse(out_atom_indices.begin(), out_atom_indices.end());
}

AtomIndexList TupleIndexMapper::to_atom_indices(TupleIndex tuple_index) const
{
    auto atom_indices = AtomIndexList {};
    to_atom_indices(tuple_index, atom_indices);
    return atom_indices;
}

std::string TupleIndexMapper::tuple_index_to_string(TupleIndex tuple_index) const
{
    auto atom_indices = to_atom_indices(tuple_index);
    std::stringstream ss;
    ss << "(";
    for (const auto atom_index : atom_indices)
    {
        ss << atom_index << ",";
    }
    ss << ")";
    return ss.str();
}

size_t TupleIndexMapper::get_num_atoms() const { return m_num_atoms; }

size_t TupleIndexMapper::get_arity() const { return m_arity; }

const std::array<size_t, MAX_ARITY>& TupleIndexMapper::get_factors() const { return m_factors; }

TupleIndex TupleIndexMapper::get_max_tuple_index() const { return get_empty_tuple_index(); }

TupleIndex TupleIndexMapper::get_empty_tuple_index() const { return (std::pow(get_num_atoms() + 1, get_arity()) - 1); }
}
