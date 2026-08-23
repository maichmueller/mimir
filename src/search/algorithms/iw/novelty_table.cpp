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
#include <cassert>
#include <limits>
#include <utility>

using namespace mimir::formalism;

namespace mimir::search::iw
{
DynamicNoveltyTable::DynamicNoveltyTable(size_t arity) :
    m_tuple_index_mapper(arity),
    m_table(BitTable(m_tuple_index_mapper.get_max_tuple_index() + 1)),
    m_state_tuple_index_generator(&m_tuple_index_mapper),
    m_state_pair_tuple_index_generator(&m_tuple_index_mapper)
{
}

DynamicNoveltyTable::DynamicNoveltyTable(size_t arity, size_t num_atoms) :
    m_tuple_index_mapper(TupleIndexMapper(arity, num_atoms)),
    m_table(BitTable(m_tuple_index_mapper.get_max_tuple_index() + 1)),
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

    auto new_table = BitTable(m_tuple_index_mapper.get_max_tuple_index() + 1);
    auto atom_indices = AtomIndexList(arity);

    for (TupleIndex tuple_index = 0; tuple_index < m_table.size(); ++tuple_index)
    {
        if (m_table.test(tuple_index))
        {
            old_tuple_index_mapper.to_atom_indices(tuple_index, atom_indices);

            for (size_t i = atom_indices.size(); i < arity; ++i)
            {
                atom_indices.push_back(new_placeholder);
            }

            const auto new_tuple_index = m_tuple_index_mapper.to_tuple_index(atom_indices);

            new_table.set(new_tuple_index);
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

        if (!m_table.test(tuple_index))
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

        if (!m_table.test(tuple_index))
        {
            out_novel_tuples.push_back(m_tuple_index_mapper.to_atom_indices(tuple_index));
        }
    }
}

void DynamicNoveltyTable::compute_novel_tuples(const State& state, const AtomIndexList& succ_state_atom_indices, std::vector<AtomIndexList>& out_novel_tuples)
{
    out_novel_tuples.clear();

    resize_to_fit(state);
    if (!succ_state_atom_indices.empty())
    {
        resize_to_fit(succ_state_atom_indices.back());
    }

    for (auto it = m_state_pair_tuple_index_generator.begin(state, succ_state_atom_indices); it != m_state_pair_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!m_table.test(tuple_index))
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

        m_table.set(tuple_index);
    }
}

bool DynamicNoveltyTable::test_novelty(const State& state)
{
    resize_to_fit(state);

    for (auto it = m_state_tuple_index_generator.begin(state); it != m_state_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!m_table.test(tuple_index))
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

        if (!m_table.test(tuple_index))
        {
            return true;
        }
    }
    return false;
}

bool DynamicNoveltyTable::test_novelty(const State& state, const AtomIndexList& succ_state_atom_indices)
{
    resize_to_fit(state);
    if (!succ_state_atom_indices.empty())
    {
        resize_to_fit(succ_state_atom_indices.back());
    }

    for (auto it = m_state_pair_tuple_index_generator.begin(state, succ_state_atom_indices); it != m_state_pair_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!m_table.test(tuple_index))
        {
            return true;
        }
    }
    return false;
}

bool DynamicNoveltyTable::test_novelty_read_only(const State& state) const
{
    const_cast<DynamicNoveltyTable*>(this)->resize_to_fit(state);

    auto state_tuple_index_generator = StateTupleIndexGenerator(&m_tuple_index_mapper);

    for (auto it = state_tuple_index_generator.begin(state); it != state_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!m_table.test(tuple_index))
        {
            return true;
        }
    }
    return false;
}

bool DynamicNoveltyTable::test_novelty_read_only(const State& state, const State& succ_state) const
{
    auto* self = const_cast<DynamicNoveltyTable*>(this);
    self->resize_to_fit(state);
    self->resize_to_fit(succ_state);

    auto state_pair_tuple_index_generator = StatePairTupleIndexGenerator(&m_tuple_index_mapper);

    for (auto it = state_pair_tuple_index_generator.begin(state, succ_state); it != state_pair_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!m_table.test(tuple_index))
        {
            return true;
        }
    }
    return false;
}

bool DynamicNoveltyTable::test_novelty_read_only(const State& state, const AtomIndexList& succ_state_atom_indices) const
{
    auto* self = const_cast<DynamicNoveltyTable*>(this);
    self->resize_to_fit(state);
    if (!succ_state_atom_indices.empty())
    {
        self->resize_to_fit(succ_state_atom_indices.back());
    }

    auto state_pair_tuple_index_generator = StatePairTupleIndexGenerator(&m_tuple_index_mapper);

    for (auto it = state_pair_tuple_index_generator.begin(state, succ_state_atom_indices); it != state_pair_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!m_table.test(tuple_index))
        {
            return true;
        }
    }
    return false;
}

bool DynamicNoveltyTable::test_atom_novelty_read_only(AtomIndex atom_index) const
{
    auto* self = const_cast<DynamicNoveltyTable*>(this);
    self->resize_to_fit(atom_index);

    const auto& tuple_index_mapper = m_tuple_index_mapper;
    auto atom_tuple = AtomIndexList { atom_index };
    const auto tuple_index = tuple_index_mapper.to_tuple_index(atom_tuple);
    assert(tuple_index < m_table.size());
    return !m_table.test(tuple_index);
}

bool DynamicNoveltyTable::test_novelty_and_update_table(const State& state)
{
    resize_to_fit(state);

    bool is_novel = false;
    for (auto it = m_state_tuple_index_generator.begin(state); it != m_state_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!is_novel && !m_table.test(tuple_index))
        {
            is_novel = true;
        }
        m_table.set(tuple_index);
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

        if (!is_novel && !m_table.test(tuple_index))
        {
            is_novel = true;
        }
        m_table.set(tuple_index);
    }
    return is_novel;
}

bool DynamicNoveltyTable::test_novelty_and_update_table(const State& state, const AtomIndexList& succ_state_atom_indices)
{
    resize_to_fit(state);
    if (!succ_state_atom_indices.empty())
    {
        resize_to_fit(succ_state_atom_indices.back());
    }

    bool is_novel = false;
    for (auto it = m_state_pair_tuple_index_generator.begin(state, succ_state_atom_indices); it != m_state_pair_tuple_index_generator.end(); ++it)
    {
        const auto tuple_index = *it;

        assert(tuple_index < m_table.size());

        if (!is_novel && !m_table.test(tuple_index))
        {
            is_novel = true;
        }
        m_table.set(tuple_index);
    }
    return is_novel;
}

void DynamicNoveltyTable::reset()
{
    for (auto& word : m_table.words)
    {
        word = 0;
    }
}
const TupleIndexMapper& DynamicNoveltyTable::get_tuple_index_mapper() const { return m_tuple_index_mapper; }

MinimumGNoveltyTable::MinimumGNoveltyTable(size_t arity) : MinimumGNoveltyTable(arity, 0) {}

MinimumGNoveltyTable::MinimumGNoveltyTable(size_t arity, size_t num_atoms) :
    m_tuple_index_mapper(arity, num_atoms),
    m_minimum_g_ranks(Ranks8(m_tuple_index_mapper.get_max_tuple_index() + 1, std::numeric_limits<uint8_t>::max())),
    m_state_tuple_index_generator(&m_tuple_index_mapper),
    m_state_pair_tuple_index_generator(&m_tuple_index_mapper)
{
}

/// The largest rank the current storage can hold; the maximum value of the element type
/// is reserved as the "never reached" marker.
static constexpr uint32_t max_rank_of(size_t element_size)
{
    return element_size == 1 ? uint32_t(std::numeric_limits<uint8_t>::max()) - 1 :
           element_size == 2 ? uint32_t(std::numeric_limits<uint16_t>::max()) - 1 :
                               std::numeric_limits<uint32_t>::max() - 1;
}

void MinimumGNoveltyTable::widen_ranks_to_fit(uint32_t rank)
{
    /// Widening copies the whole table, so it is done only when a search actually sees
    /// more distinct g values than the current element type can index. With unit costs
    /// that means a plan depth beyond 254, and then beyond 65534.
    const auto widen = [&](const auto& from, auto&& to)
    {
        using ToVec = std::decay_t<decltype(to)>;
        using ToElem = typename ToVec::value_type;
        using FromElem = typename std::decay_t<decltype(from)>::value_type;
        constexpr auto from_none = std::numeric_limits<FromElem>::max();
        constexpr auto to_none = std::numeric_limits<ToElem>::max();
        to.assign(from.size(), to_none);
        for (size_t i = 0; i < from.size(); ++i)
        {
            if (from[i] != from_none)
            {
                to[i] = static_cast<ToElem>(from[i]);
            }
        }
    };

    if (std::holds_alternative<Ranks8>(m_minimum_g_ranks) && rank > max_rank_of(1))
    {
        auto widened = Ranks16 {};
        widen(std::get<Ranks8>(m_minimum_g_ranks), widened);
        m_minimum_g_ranks = std::move(widened);
    }
    if (std::holds_alternative<Ranks16>(m_minimum_g_ranks) && rank > max_rank_of(2))
    {
        auto widened = Ranks32 {};
        widen(std::get<Ranks16>(m_minimum_g_ranks), widened);
        m_minimum_g_ranks = std::move(widened);
    }
}

uint32_t MinimumGNoveltyTable::get_or_create_rank(ContinuousCost g_value)
{
    const auto [it, inserted] = m_g_value_to_rank.try_emplace(g_value, static_cast<uint32_t>(m_g_values.size()));
    if (inserted)
    {
        m_g_values.push_back(g_value);
        widen_ranks_to_fit(it->second);
    }
    return it->second;
}

std::optional<uint32_t> MinimumGNoveltyTable::find_rank(ContinuousCost g_value) const
{
    const auto it = m_g_value_to_rank.find(g_value);
    return it == m_g_value_to_rank.end() ? std::nullopt : std::optional<uint32_t>(it->second);
}

void MinimumGNoveltyTable::resize_to_fit(AtomIndex atom_index)
{
    if (atom_index < m_tuple_index_mapper.get_num_atoms())
    {
        return;
    }

    const auto arity = m_tuple_index_mapper.get_arity();
    auto new_size = std::max(size_t(1), m_tuple_index_mapper.get_num_atoms());
    while (new_size < atom_index + 2)
    {
        new_size *= 2;
    }

    const auto new_placeholder = new_size;
    const auto old_tuple_index_mapper = m_tuple_index_mapper;
    m_tuple_index_mapper.initialize(arity, new_size);

    /// Remap every labelled tuple into the wider index space, keeping the smaller label
    /// where two old tuples collapse onto one new index.
    std::visit(
        [&](auto& old_ranks)
        {
            using Vec = std::decay_t<decltype(old_ranks)>;
            using Elem = typename Vec::value_type;
            constexpr auto none = std::numeric_limits<Elem>::max();

            auto new_ranks = Vec(m_tuple_index_mapper.get_max_tuple_index() + 1, none);
            auto atom_indices = AtomIndexList {};
            atom_indices.reserve(arity);
            for (TupleIndex tuple_index = 0; tuple_index < old_ranks.size(); ++tuple_index)
            {
                const auto old_rank = old_ranks[tuple_index];
                if (old_rank == none)
                {
                    continue;
                }

                old_tuple_index_mapper.to_atom_indices(tuple_index, atom_indices);
                for (size_t i = atom_indices.size(); i < arity; ++i)
                {
                    atom_indices.push_back(new_placeholder);
                }
                const auto new_tuple_index = m_tuple_index_mapper.to_tuple_index(atom_indices);
                auto& slot = new_ranks[new_tuple_index];
                if (slot == none || m_g_values[old_rank] < m_g_values[slot])
                {
                    slot = old_rank;
                }
            }
            old_ranks = std::move(new_ranks);
        },
        m_minimum_g_ranks);
}

void MinimumGNoveltyTable::resize_to_fit(const State& state)
{
    const auto& fluent_atoms = state.get_atoms<FluentTag>();
    const auto it = std::max_element(fluent_atoms.begin(), fluent_atoms.end());
    if (it != fluent_atoms.end())
    {
        resize_to_fit(*it);
    }
}

/// Shared inner loop of both update overloads: one `std::visit` per call, so the loop over
/// tuple indices itself stays monomorphic and free of bounds checks.
template<typename Generator, typename... Args>
bool MinimumGNoveltyTable::update_from(Generator& generator, ContinuousCost g_value, Args&&... args)
{
    const auto rank = get_or_create_rank(g_value);

    return std::visit(
        [&](auto& ranks)
        {
            using Elem = typename std::decay_t<decltype(ranks)>::value_type;
            constexpr auto none = std::numeric_limits<Elem>::max();
            const auto new_rank = static_cast<Elem>(rank);
            const auto* g_values = m_g_values.data();

            auto improved = false;
            for (auto it = generator.begin(std::forward<Args>(args)...); it != generator.end(); ++it)
            {
                const auto tuple_index = *it;
                assert(tuple_index < ranks.size());
                auto& slot = ranks[tuple_index];
                if (slot == none)
                {
                    slot = new_rank;
                    improved = true;
                }
                else if (g_value < g_values[slot])
                {
                    slot = new_rank;
                    improved = true;
                    m_lowered_existing_label = true;
                }
            }
            return improved;
        },
        m_minimum_g_ranks);
}

bool MinimumGNoveltyTable::test_novelty_and_update_table(const State& state, ContinuousCost g_value)
{
    resize_to_fit(state);
    if (state.get_atoms<FluentTag>().count() + 1 < m_tuple_index_mapper.get_arity())
    {
        return false;
    }
    return update_from(m_state_tuple_index_generator, g_value, state);
}

bool MinimumGNoveltyTable::test_novelty_and_update_table(const State& state, const State& succ_state, ContinuousCost g_value)
{
    resize_to_fit(state);
    resize_to_fit(succ_state);
    if (succ_state.get_atoms<FluentTag>().count() + 1 < m_tuple_index_mapper.get_arity())
    {
        return false;
    }
    return update_from(m_state_pair_tuple_index_generator, g_value, state, succ_state);
}

bool MinimumGNoveltyTable::test_would_improve(const State& state, const State& succ_state, ContinuousCost g_value)
{
    resize_to_fit(state);
    resize_to_fit(succ_state);
    if (succ_state.get_atoms<FluentTag>().count() + 1 < m_tuple_index_mapper.get_arity())
    {
        return false;
    }
    /// Deliberately no rank lookup: the predicate is "is this cost below the tuple's own", which
    /// reads the label's cost and never needs a rank for `g_value`. Minting one here would both
    /// mutate a read-only path and make the answer depend on which costs happen to have been
    /// recorded rather than on the labels themselves.
    return std::visit(
        [&](const auto& ranks)
        {
            using Elem = typename std::decay_t<decltype(ranks)>::value_type;
            constexpr auto none = std::numeric_limits<Elem>::max();
            const auto* g_values = m_g_values.data();

            for (auto it = m_state_pair_tuple_index_generator.begin(state, succ_state); it != m_state_pair_tuple_index_generator.end(); ++it)
            {
                const auto tuple_index = *it;
                assert(tuple_index < ranks.size());
                const auto slot = ranks[tuple_index];
                if (slot == none || g_value < g_values[slot])
                {
                    return true;
                }
            }
            return false;
        },
        m_minimum_g_ranks);
}

bool MinimumGNoveltyTable::test_novelty_at_g_read_only(const State& state, ContinuousCost g_value)
{
    resize_to_fit(state);
    if (state.get_atoms<FluentTag>().count() + 1 < m_tuple_index_mapper.get_arity())
    {
        return false;
    }
    /// A cost never recorded cannot be any tuple's label, and looking it up must not
    /// create a rank for it.
    const auto rank = find_rank(g_value);
    if (!rank)
    {
        return false;
    }

    return std::visit(
        [&](const auto& ranks)
        {
            using Elem = typename std::decay_t<decltype(ranks)>::value_type;
            const auto wanted = static_cast<Elem>(*rank);
            for (auto it = m_state_tuple_index_generator.begin(state); it != m_state_tuple_index_generator.end(); ++it)
            {
                const auto tuple_index = *it;
                assert(tuple_index < ranks.size());
                if (ranks[tuple_index] == wanted)
                {
                    return true;
                }
            }
            return false;
        },
        m_minimum_g_ranks);
}

const TupleIndexMapper& MinimumGNoveltyTable::get_tuple_index_mapper() const { return m_tuple_index_mapper; }
}
