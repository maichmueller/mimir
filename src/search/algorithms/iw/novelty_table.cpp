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

#include "mimir/search/algorithms/iw/novelty_table.hpp"
#include "mimir/search/state.hpp"

#include <algorithm>

using namespace mimir::formalism;

namespace mimir::search::iw
{
DynamicNoveltyTable::DynamicNoveltyTable(size_t arity) :
    m_tuple_index_mapper(arity),
    m_table(std::vector<bool>(m_tuple_index_mapper.get_max_tuple_index() + 1, false)),
    m_state_tuple_index_generator(&m_tuple_index_mapper),
    m_state_pair_tuple_index_generator(&m_tuple_index_mapper)
{
}

DynamicNoveltyTable::DynamicNoveltyTable(size_t arity, size_t num_atoms) :
    m_tuple_index_mapper(TupleIndexMapper(arity, num_atoms)),
    m_table(std::vector<bool>(m_tuple_index_mapper.get_max_tuple_index() + 1, false)),
    m_state_tuple_index_generator(&m_tuple_index_mapper),
    m_state_pair_tuple_index_generator(&m_tuple_index_mapper)
{
}

void DynamicNoveltyTable::resize_to_fit(AtomIndex atom_index)
{
    if (atom_index < m_tuple_index_mapper.get_num_atoms())
    {
        return;
    }

    const auto arity = m_tuple_index_mapper.get_arity();

    auto new_size = std::max(size_t(1), m_tuple_index_mapper.get_num_atoms());
    while (new_size < atom_index + 1 + 1)
    {
        new_size *= 2;
    }
    const auto new_placeholder = new_size;

    const auto old_tuple_index_mapper = m_tuple_index_mapper;

    m_tuple_index_mapper.initialize(arity, new_size);

    auto new_table = std::vector<bool>(m_tuple_index_mapper.get_max_tuple_index() + 1, false);
    auto atom_indices = AtomIndexList(arity);

    for (TupleIndex tuple_index = 0; tuple_index < m_table.size(); ++tuple_index)
    {
        if (m_table[tuple_index])
        {
            old_tuple_index_mapper.to_atom_indices(tuple_index, atom_indices);

            for (size_t i = atom_indices.size(); i < arity; ++i)
            {
                atom_indices.push_back(new_placeholder);
            }

            const auto new_tuple_index = m_tuple_index_mapper.to_tuple_index(atom_indices);

            new_table[new_tuple_index] = true;
        }
    }

    m_table = std::move(new_table);
}

void DynamicNoveltyTable::resize_to_fit(const State& state)
{
    const auto& fluent_atoms = state.get_atoms<FluentTag>();

    const auto it = std::max_element(fluent_atoms.begin(), fluent_atoms.end());

    if (it == fluent_atoms.end())
    {
        return;
    }

    resize_to_fit(*it);
}

void DynamicNoveltyTable::compute_novel_tuples(const State& state, std::vector<AtomIndexList>& out_novel_tuples)
{
    out_novel_tuples.clear();

    resize_to_fit(state);

    for (auto it = m_state_tuple_index_generator.begin(state); it != m_state_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!m_table[tuple_index])
        {
            out_novel_tuples.push_back(m_tuple_index_mapper.to_atom_indices(tuple_index));
        }
    }
}

void DynamicNoveltyTable::compute_novel_tuples(const State& state, const State& succ_state, std::vector<AtomIndexList>& out_novel_tuples)
{
    out_novel_tuples.clear();

    resize_to_fit(state);
    resize_to_fit(succ_state);

    for (auto it = m_state_pair_tuple_index_generator.begin(state, succ_state); it != m_state_pair_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!m_table[tuple_index])
        {
            out_novel_tuples.push_back(m_tuple_index_mapper.to_atom_indices(tuple_index));
        }
    }
}

void DynamicNoveltyTable::insert_tuples(const std::vector<AtomIndexList>& tuples)
{
    for (const auto& tuple : tuples)
    {
        const auto tuple_index = m_tuple_index_mapper.to_tuple_index(tuple);

        assert(tuple_index < m_table.size());

        m_table[tuple_index] = true;
    }
}

bool DynamicNoveltyTable::test_novelty(const State& state)
{
    resize_to_fit(state);

    for (auto it = m_state_tuple_index_generator.begin(state); it != m_state_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!m_table[tuple_index])
        {
            return true;
        }
    }
    return false;
}

bool DynamicNoveltyTable::test_novelty(const State& state, const State& succ_state)
{
    resize_to_fit(state);
    resize_to_fit(succ_state);

    for (auto it = m_state_pair_tuple_index_generator.begin(state, succ_state); it != m_state_pair_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!m_table[tuple_index])
        {
            return true;
        }
    }
    return false;
}

bool DynamicNoveltyTable::test_novelty_read_only(const State& state) const
{
    auto state_tuple_index_generator = StateTupleIndexGenerator(&m_tuple_index_mapper);

    for (auto it = state_tuple_index_generator.begin(state); it != state_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!m_table[tuple_index])
        {
            return true;
        }
    }
    return false;
}

bool DynamicNoveltyTable::test_novelty_read_only(const State& state, const State& succ_state) const
{
    auto state_pair_tuple_index_generator = StatePairTupleIndexGenerator(&m_tuple_index_mapper);

    for (auto it = state_pair_tuple_index_generator.begin(state, succ_state); it != state_pair_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!m_table[tuple_index])
        {
            return true;
        }
    }
    return false;
}

bool DynamicNoveltyTable::test_novelty_and_update_table(const State& state)
{
    resize_to_fit(state);

    bool is_novel = false;
    for (auto it = m_state_tuple_index_generator.begin(state); it != m_state_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!is_novel && !m_table[tuple_index])
        {
            is_novel = true;
        }
        m_table[tuple_index] = true;
    }
    return is_novel;
}

bool DynamicNoveltyTable::test_novelty_and_update_table(const State& state, const State& succ_state)
{
    resize_to_fit(state);
    resize_to_fit(succ_state);

    bool is_novel = false;
    for (auto it = m_state_pair_tuple_index_generator.begin(state, succ_state); it != m_state_pair_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!is_novel && !m_table[tuple_index])
        {
            is_novel = true;
        }
        m_table[tuple_index] = true;
    }
    return is_novel;
}

void DynamicNoveltyTable::reset() { std::fill(m_table.begin(), m_table.end(), false); }
const TupleIndexMapper& DynamicNoveltyTable::get_tuple_index_mapper() const { return m_tuple_index_mapper; }
}
