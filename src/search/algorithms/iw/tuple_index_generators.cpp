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

#include "mimir/search/algorithms/iw/tuple_index_generators.hpp"
#include "mimir/search/state.hpp"

#include <algorithm>
#include <limits>

using namespace mimir::formalism;

namespace mimir::search::iw
{
StateTupleIndexGenerator::StateTupleIndexGenerator(const TupleIndexMapper* tuple_index_mapper_) : tuple_index_mapper(tuple_index_mapper_), atom_indices() {}

StateTupleIndexGenerator::const_iterator::const_iterator() : m_tuple_index_mapper(nullptr), m_atoms(nullptr) {}

StateTupleIndexGenerator::const_iterator::const_iterator(StateTupleIndexGenerator* stig, bool begin) :
    m_tuple_index_mapper(begin ? stig->tuple_index_mapper : nullptr),
    m_atoms(begin ? &stig->atom_indices : nullptr),
    m_end(!begin),
    m_cur(begin ? 0 : -1)
{
    if (begin)
    {
        const auto arity = m_tuple_index_mapper->get_arity();
        const auto& factors = m_tuple_index_mapper->get_factors();

        assert(!m_atoms->empty());
        assert(std::is_sorted(m_atoms->begin(), m_atoms->end()));

        for (size_t i = 0; i < arity; ++i)
        {
            m_indices[i] = i;
            m_cur += (*m_atoms)[i] * factors[i];
        }
    }
    else
    {
        assert(!stig);
    }
}

std::optional<size_t> StateTupleIndexGenerator::const_iterator::find_rightmost_incrementable_index()
{
    const auto arity = get_tuple_index_mapper().get_arity();

    int i = static_cast<int>(arity) - 1;
    while (i >= 0 && (m_indices[i] == get_atoms().size() - 1))
    {
        --i;
    }
    return (i < 0) ? std::nullopt : std::optional<size_t>(i);
}

void StateTupleIndexGenerator::const_iterator::advance()
{
    const auto arity = get_tuple_index_mapper().get_arity();
    const auto& factors = get_tuple_index_mapper().get_factors();
    const auto num_atoms = get_atoms().size();

    const auto rightmost_incrementable_index = find_rightmost_incrementable_index();
    if (!rightmost_incrementable_index)
    {
        m_end = true;
        ++m_cur;
        return;
    }
    const auto i = rightmost_incrementable_index.value();

    const auto index = ++m_indices[i];
    const auto diff_i = get_atoms()[index] - get_atoms()[index - 1];
    m_cur += diff_i * factors[i];

    for (size_t j = i + 1; j < arity; ++j)
    {
        size_t old_index = m_indices[j];
        size_t new_index = m_indices[j] = std::min(num_atoms - 1, m_indices[j - 1] + 1);
        const auto diff_j = get_atoms()[new_index] - get_atoms()[old_index];
        m_cur += diff_j * factors[j];
    }
}

const TupleIndexMapper& StateTupleIndexGenerator::const_iterator::get_tuple_index_mapper() const
{
    assert(m_tuple_index_mapper);
    return *m_tuple_index_mapper;
}

const AtomIndexList& StateTupleIndexGenerator::const_iterator::get_atoms() const
{
    assert(m_atoms);
    return *m_atoms;
}

StateTupleIndexGenerator::const_iterator::value_type StateTupleIndexGenerator::const_iterator::operator*() const
{
    assert(m_tuple_index_mapper && m_atoms);
    assert(m_cur >= 0);
    return m_cur;
}

StateTupleIndexGenerator::const_iterator& StateTupleIndexGenerator::const_iterator::operator++()
{
    advance();
    return *this;
}

StateTupleIndexGenerator::const_iterator StateTupleIndexGenerator::const_iterator::operator++(int)
{
    const_iterator tmp = *this;
    ++(*this);
    return tmp;
}

bool StateTupleIndexGenerator::const_iterator::operator==(const const_iterator& other) const
{
    if (m_end == other.m_end)
    {
        return true;
    }

    return m_cur == other.m_cur;
}

bool StateTupleIndexGenerator::const_iterator::operator!=(const const_iterator& other) const { return !(*this == other); }

StateTupleIndexGenerator::const_iterator StateTupleIndexGenerator::begin(const AtomIndexList& atom_indices_)
{
    atom_indices = atom_indices_;
    assert(std::is_sorted(atom_indices.begin(), atom_indices.end()));

    return const_iterator(this, true);
}

StateTupleIndexGenerator::const_iterator StateTupleIndexGenerator::begin(const State& state)
{
    atom_indices.clear();

    const auto& fluent_atoms = state.get_atoms<FluentTag>();
    atom_indices.insert(atom_indices.end(), fluent_atoms.begin(), fluent_atoms.end());
    atom_indices.push_back(tuple_index_mapper->get_num_atoms());
    assert(std::is_sorted(atom_indices.begin(), atom_indices.end()));

    return const_iterator(this, true);
}

StateTupleIndexGenerator::const_iterator StateTupleIndexGenerator::end() const { return const_iterator(nullptr, false); }

StatePairTupleIndexGenerator::StatePairTupleIndexGenerator(const TupleIndexMapper* tuple_index_mapper_) : tuple_index_mapper(tuple_index_mapper_) {}

StatePairTupleIndexGenerator::const_iterator::const_iterator() :
    m_tuple_index_mapper(nullptr),
    m_a_atoms(nullptr),
    m_a_jumpers(nullptr),
    m_indices(),
    m_a(),
    m_cur_outter(-1),
    m_cur_inner(-1),
    m_end_outter(false),
    m_end_inner(false)
{
}

StatePairTupleIndexGenerator::const_iterator::const_iterator(StatePairTupleIndexGenerator* sptig, bool begin) :
    m_tuple_index_mapper(begin ? sptig->tuple_index_mapper : nullptr),
    m_a_atoms(begin ? &sptig->a_atom_indices : nullptr),
    m_a_jumpers(begin ? &sptig->a_index_jumper : nullptr),
    m_indices(),
    m_a(),
    m_cur_outter(begin ? 0 : -1),
    m_cur_inner(begin ? 0 : -1),
    m_end_outter(begin ? false : true),
    m_end_inner(begin ? false : true)
{
    if (begin)
    {
        assert(!get_atoms()[0].empty());
        assert(!get_atoms()[1].empty());
        assert(std::is_sorted(get_atoms()[0].begin(), get_atoms()[0].end()));
        assert(std::is_sorted(get_atoms()[1].begin(), get_atoms()[1].end()));

        initialize_jumpers();
        advance_outter();
    }
    else
    {
        assert(!sptig);
    }
}

void StatePairTupleIndexGenerator::const_iterator::initialize_jumpers()
{
    get_jumpers()[0].clear();
    get_jumpers()[1].clear();
    get_jumpers()[0].resize(get_atoms()[0].size(), std::numeric_limits<size_t>::max());
    get_jumpers()[1].resize(get_atoms()[1].size(), std::numeric_limits<size_t>::max());

    size_t j = 0;
    size_t i = 0;
    while (j < get_atoms()[0].size() && i < get_atoms()[1].size())
    {
        if (get_atoms()[0][j] < get_atoms()[1][i])
        {
            get_jumpers()[0][j] = i;
            ++j;
        }
        else if (get_atoms()[0][j] > get_atoms()[1][i])
        {
            get_jumpers()[1][i] = j;
            ++i;
        }
        else
        {
            get_jumpers()[1][i] = j;
            get_jumpers()[0][j] = i;
            ++j;
            ++i;
        }
    }
}

static void compute_binary_representation(size_t value, size_t arity, std::array<bool, MAX_ARITY>& output)
{
    for (size_t i = 0; i < arity; ++i)
    {
        output[i] = (value & (1 << i)) != 0;
    }
}

std::optional<size_t> StatePairTupleIndexGenerator::const_iterator::find_rightmost_incrementable_index()
{
    const int arity = get_tuple_index_mapper().get_arity();
    int i = arity - 1;

    if (m_indices[i] < get_atoms()[m_a[i]].size() - 1)
    {
        return i;
    }
    --i;

    while (i >= 0 && ((m_indices[i] == get_atoms()[m_a[i]].size() - 1) || (get_atoms()[m_a[i]][m_indices[i] + 1] >= get_atoms()[m_a[i + 1]][m_indices[i + 1]])))
    {
        --i;
    }
    return (i < 0) ? std::nullopt : std::optional<size_t>(i);
}

std::optional<size_t> StatePairTupleIndexGenerator::const_iterator::find_next_index(size_t i)
{
    if (m_a[i - 1] == m_a[i])
    {
        if (!m_a[i])
        {
            return std::min(get_atoms()[m_a[i]].size() - 1, m_indices[i - 1] + 1);
        }
        else
        {
            if (m_indices[i - 1] == get_atoms()[m_a[i]].size() - 1)
            {
                return std::nullopt;
            }
            else
            {
                return m_indices[i - 1] + 1;
            }
        }
    }
    else
    {
        if (!m_a[i])
        {
            return std::min(get_atoms()[m_a[i]].size() - 1, get_jumpers()[m_a[i - 1]][m_indices[i - 1]]);
        }
        else
        {
            if (get_jumpers()[m_a[i - 1]][m_indices[i - 1]] == std::numeric_limits<size_t>::max())
            {
                return std::nullopt;
            }
            else
            {
                return get_jumpers()[m_a[i - 1]][m_indices[i - 1]];
            }
        }
    }
    return std::nullopt;
}

bool StatePairTupleIndexGenerator::const_iterator::advance_outter()
{
    const auto arity = get_tuple_index_mapper().get_arity();

    ++m_cur_outter;

    for (; m_cur_outter < (1 << arity); ++m_cur_outter)
    {
        compute_binary_representation(m_cur_outter, arity, m_a);

        if (try_create_first_inner_tuple())
        {
            m_end_inner = false;
            return true;
        }
    }

    m_end_inner = true;
    m_end_outter = true;
    return false;
}

void StatePairTupleIndexGenerator::const_iterator::advance_inner()
{
    while (true)
    {
        if (m_end_inner)
        {
            advance_outter();
            return;
        }

        const auto right_most_incrementable_index = find_rightmost_incrementable_index();

        if (!right_most_incrementable_index)
        {
            m_end_inner = true;
            continue;
        }
        const auto i = right_most_incrementable_index.value();

        if (!try_create_next_inner_tuple(i))
        {
            m_end_inner = true;
            continue;
        }

        return;
    }
}

bool StatePairTupleIndexGenerator::const_iterator::try_create_first_inner_tuple()
{
    const auto arity = get_tuple_index_mapper().get_arity();
    const auto& factors = get_tuple_index_mapper().get_factors();

    m_indices[0] = 0;
    const auto atom_0 = get_atoms()[m_a[0]][0];
    m_cur_inner = atom_0 * factors[0];

    for (size_t j = 1; j < arity; ++j)
    {
        const auto next_index = find_next_index(j);

        if (!next_index)
        {
            return false;
        }
        const auto new_index = next_index.value();

        m_indices[j] = new_index;
        const auto atom_j = get_atoms()[m_a[j]][new_index];
        m_cur_inner += atom_j * factors[j];
    }

    return true;
}

bool StatePairTupleIndexGenerator::const_iterator::try_create_next_inner_tuple(size_t i)
{
    const auto arity = get_tuple_index_mapper().get_arity();
    const auto& factors = get_tuple_index_mapper().get_factors();

    const auto index = ++m_indices[i];
    const auto diff_i = (get_atoms()[m_a[i]][index] - get_atoms()[m_a[i]][index - 1]);
    m_cur_inner += diff_i * factors[i];

    for (size_t j = i + 1; j < arity; ++j)
    {
        const auto old_index = m_indices[j];

        const auto next_index = find_next_index(j);
        if (!next_index)
        {
            return false;
        }
        const auto new_index = next_index.value();

        m_indices[j] = new_index;
        const auto diff_j = (get_atoms()[m_a[j]][new_index] - get_atoms()[m_a[j]][old_index]);
        m_cur_inner += diff_j * factors[j];
    }

    return true;
}

const TupleIndexMapper& StatePairTupleIndexGenerator::const_iterator::get_tuple_index_mapper() const
{
    assert(m_tuple_index_mapper);
    return *m_tuple_index_mapper;
}

const std::array<AtomIndexList, 2>& StatePairTupleIndexGenerator::const_iterator::get_atoms() const
{
    assert(m_a_atoms);
    return *m_a_atoms;
}

std::array<std::vector<size_t>, 2>& StatePairTupleIndexGenerator::const_iterator::get_jumpers() const
{
    assert(m_a_jumpers);
    return *m_a_jumpers;
}

StatePairTupleIndexGenerator::const_iterator::value_type StatePairTupleIndexGenerator::const_iterator::operator*() const
{
    assert(m_tuple_index_mapper && m_a_atoms && m_a_jumpers);
    assert(m_cur_inner >= 0);
    return m_cur_inner;
}

StatePairTupleIndexGenerator::const_iterator& StatePairTupleIndexGenerator::const_iterator::operator++()
{
    advance_inner();
    return *this;
}

StatePairTupleIndexGenerator::const_iterator StatePairTupleIndexGenerator::const_iterator::operator++(int)
{
    const_iterator tmp = *this;
    ++(*this);
    return tmp;
}

bool StatePairTupleIndexGenerator::const_iterator::operator==(const const_iterator& other) const
{
    if (m_end_inner == other.m_end_inner && m_end_outter == other.m_end_outter)
    {
        return true;
    }
    return m_cur_outter == other.m_cur_outter && m_cur_inner == other.m_cur_inner;
}

bool StatePairTupleIndexGenerator::const_iterator::operator!=(const const_iterator& other) const { return !(*this == other); }

StatePairTupleIndexGenerator::const_iterator StatePairTupleIndexGenerator::begin(const State& state, const State& succ_state)
{
    a_atom_indices[0].clear();
    a_atom_indices[1].clear();
    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    const auto& succ_state_fluent_atoms = succ_state.get_atoms<FluentTag>();

    auto it1 = succ_state_fluent_atoms.begin();
    auto it2 = state_fluent_atoms.begin();

    while (it1 != succ_state_fluent_atoms.end() && it2 != state_fluent_atoms.end())
    {
        if (*it1 < *it2)
        {
            a_atom_indices[1].push_back(*it1);
            ++it1;
        }
        else if (*it2 < *it1)
        {
            ++it2;
        }
        else
        {
            a_atom_indices[0].push_back(*it1);
            ++it1;
            ++it2;
        }
    }
    for (; it1 != succ_state_fluent_atoms.end(); ++it1)
    {
        a_atom_indices[1].push_back(*it1);
    }

    a_atom_indices[0].push_back(tuple_index_mapper->get_num_atoms());

    assert(std::is_sorted(a_atom_indices[0].begin(), a_atom_indices[0].end()));
    assert(std::is_sorted(a_atom_indices[1].begin(), a_atom_indices[1].end()));

    return a_atom_indices[1].empty() ? const_iterator(nullptr, false) : const_iterator(this, true);
}

StatePairTupleIndexGenerator::const_iterator StatePairTupleIndexGenerator::begin(const State& state, const AtomIndexList& succ_atom_indices)
{
    a_atom_indices[0].clear();
    a_atom_indices[1].clear();
    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();

    auto it1 = succ_atom_indices.begin();
    auto it2 = state_fluent_atoms.begin();

    while (it1 != succ_atom_indices.end() && it2 != state_fluent_atoms.end())
    {
        if (*it1 < *it2)
        {
            a_atom_indices[1].push_back(*it1);
            ++it1;
        }
        else if (*it2 < *it1)
        {
            ++it2;
        }
        else
        {
            a_atom_indices[0].push_back(*it1);
            ++it1;
            ++it2;
        }
    }
    for (; it1 != succ_atom_indices.end(); ++it1)
    {
        a_atom_indices[1].push_back(*it1);
    }

    a_atom_indices[0].push_back(tuple_index_mapper->get_num_atoms());

    assert(std::is_sorted(a_atom_indices[0].begin(), a_atom_indices[0].end()));
    assert(std::is_sorted(a_atom_indices[1].begin(), a_atom_indices[1].end()));

    return a_atom_indices[1].empty() ? const_iterator(nullptr, false) : const_iterator(this, true);
}

StatePairTupleIndexGenerator::const_iterator StatePairTupleIndexGenerator::begin(const AtomIndexList& atom_indices, const AtomIndexList& add_atom_indices)
{
    a_atom_indices[0] = atom_indices;
    a_atom_indices[1] = add_atom_indices;
    assert(std::is_sorted(a_atom_indices[0].begin(), a_atom_indices[0].end()));
    assert(std::is_sorted(a_atom_indices[1].begin(), a_atom_indices[1].end()));

    return a_atom_indices[1].empty() ? const_iterator(nullptr, false) : const_iterator(this, true);
}

StatePairTupleIndexGenerator::const_iterator StatePairTupleIndexGenerator::end() const { return const_iterator(nullptr, false); }
}
