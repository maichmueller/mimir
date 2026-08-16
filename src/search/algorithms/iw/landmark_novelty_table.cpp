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

#include "mimir/search/algorithms/iw/landmark_novelty_table.hpp"

#include "mimir/search/state.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <stdexcept>

using namespace mimir::formalism;

namespace mimir::search::iw
{

namespace
{
AtomIndexList normalize_landmark_atom_indices(AtomIndexList landmark_atom_indices)
{
    std::sort(landmark_atom_indices.begin(), landmark_atom_indices.end());
    landmark_atom_indices.erase(std::unique(landmark_atom_indices.begin(), landmark_atom_indices.end()), landmark_atom_indices.end());
    return landmark_atom_indices;
}

/// @brief Grow an atom capacity by doubling until `atom_index` fits alongside the placeholder.
size_t grown_num_atoms(size_t current_num_atoms, AtomIndex atom_index)
{
    auto new_size = std::max(size_t(1), current_num_atoms);
    while (new_size < atom_index + 1 + 1)
    {
        new_size *= 2;
    }
    return new_size;
}

/// @brief Whether `state` holds any fluent atom whose index is at or beyond `capacity`.
///
/// The question `resize_to_fit` actually needs, and much cheaper to answer than the maximum: only
/// the words at or past `capacity` are inspected, and none at all when the bitset does not reach
/// that far -- the usual case, since tables are built over the whole fluent atom universe.
bool has_fluent_atom_at_or_beyond(const State& state, size_t capacity)
{
    const auto& fluent_atoms = state.get_atoms<FluentTag>();
    if (capacity >= fluent_atoms.blocks().size() * FlatBitset::block_size)
    {
        return false;
    }
    return fluent_atoms.next_set_bit(capacity) != FlatBitset::no_position;
}

/// @brief Largest fluent atom index of `state`, if any.
std::optional<AtomIndex> max_fluent_atom_index(const State& state)
{
    const auto& fluent_atoms = state.get_atoms<FluentTag>();
    const auto it = std::max_element(fluent_atoms.begin(), fluent_atoms.end());
    return (it == fluent_atoms.end()) ? std::nullopt : std::optional<AtomIndex>(*it);
}
}

/**
 * LandmarkCoordinates
 */

LandmarkCoordinates::LandmarkCoordinates(AtomIndexList landmark_atom_indices) :
    m_landmark_atom_indices(normalize_landmark_atom_indices(std::move(landmark_atom_indices))),
    m_rank_by_atom_index()
{
    if (!m_landmark_atom_indices.empty())
    {
        m_rank_by_atom_index.assign(m_landmark_atom_indices.back() + 1, NOT_A_LANDMARK);
        for (size_t rank = 0; rank < m_landmark_atom_indices.size(); ++rank)
        {
            m_rank_by_atom_index[m_landmark_atom_indices[rank]] = static_cast<uint32_t>(rank);
        }
    }
}

uint32_t LandmarkCoordinates::get_rank(AtomIndex atom_index) const
{
    return (atom_index < m_rank_by_atom_index.size()) ? m_rank_by_atom_index[atom_index] : NOT_A_LANDMARK;
}

void LandmarkCoordinates::collect(const State& state, std::vector<uint32_t>& out_ranks) const
{
    out_ranks.clear();

    const auto& fluent_atoms = state.get_atoms<FluentTag>();
    for (const auto atom_index : m_landmark_atom_indices)
    {
        if (fluent_atoms.get(atom_index))
        {
            out_ranks.push_back(m_rank_by_atom_index[atom_index]);
        }
    }

    if (out_ranks.empty())
    {
        out_ranks.push_back(get_bot_rank());
    }
}

void LandmarkCoordinates::collect_transition(const State& state,
                                             const State& succ_state,
                                             std::vector<uint32_t>& out_flipped_ranks,
                                             std::vector<uint32_t>& out_kept_ranks) const
{
    out_flipped_ranks.clear();
    out_kept_ranks.clear();

    const auto& fluent_atoms = state.get_atoms<FluentTag>();
    const auto& succ_fluent_atoms = succ_state.get_atoms<FluentTag>();

    auto any_true_in_state = false;
    auto any_true_in_succ = false;

    for (const auto atom_index : m_landmark_atom_indices)
    {
        const auto in_state = fluent_atoms.get(atom_index);
        const auto in_succ = succ_fluent_atoms.get(atom_index);
        any_true_in_state = any_true_in_state || in_state;

        if (in_succ)
        {
            any_true_in_succ = true;
            (in_state ? out_kept_ranks : out_flipped_ranks).push_back(m_rank_by_atom_index[atom_index]);
        }
    }

    /* If the predecessor held a landmark and the successor holds none, BOT has just become the
       successor's coordinate and must pair with all free tuples, not only with new ones. */
    if (!any_true_in_succ)
    {
        (any_true_in_state ? out_flipped_ranks : out_kept_ranks).push_back(get_bot_rank());
    }
}

void LandmarkCoordinates::collect_true_landmark_atoms(const State& state, AtomIndexList& out_atom_indices) const
{
    out_atom_indices.clear();

    const auto& fluent_atoms = state.get_atoms<FluentTag>();
    for (const auto atom_index : m_landmark_atom_indices)
    {
        if (fluent_atoms.get(atom_index))
        {
            out_atom_indices.push_back(atom_index);
        }
    }
}

void LandmarkCoordinates::collect_transition_from_delta(const AtomIndexList& true_landmark_atom_indices,
                                                        const AtomIndexList& add_atom_indices,
                                                        const AtomIndexList& del_atom_indices,
                                                        std::vector<uint32_t>& out_flipped_ranks,
                                                        std::vector<uint32_t>& out_kept_ranks) const
{
    out_flipped_ranks.clear();
    out_kept_ranks.clear();

    if (m_landmark_atom_indices.empty())
    {
        /* Degenerate LIW: BOT is the only coordinate and is true in every state, so it is always
           kept. Bail out before looking at the transition at all. */
        out_kept_ranks.push_back(get_bot_rank());
        return;
    }

    /* Only atoms the action touches can change a coordinate, so the added landmarks are the whole
       flipped set: a landmark in `add_atom_indices` was false in the predecessor by construction. */
    for (const auto atom_index : add_atom_indices)
    {
        const auto rank = get_rank(atom_index);
        if (rank != NOT_A_LANDMARK)
        {
            out_flipped_ranks.push_back(rank);
        }
    }

    /* The kept coordinates are the true landmarks the action does not delete. Both lists are tiny
       and sorted, so this is a linear merge rather than a membership structure. */
    auto it_del = del_atom_indices.begin();
    for (const auto atom_index : true_landmark_atom_indices)
    {
        for (; (it_del != del_atom_indices.end()) && (*it_del < atom_index); ++it_del)
        {
        }
        if ((it_del != del_atom_indices.end()) && (*it_del == atom_index))
        {
            ++it_del;
            continue;
        }
        out_kept_ranks.push_back(m_rank_by_atom_index[atom_index]);
    }

    /* BOT is a coordinate of the successor exactly when it holds no real landmark, and it flips on
       exactly when the predecessor did hold one. Both conjuncts are needed: deleting every true
       landmark does not produce BOT if the same action makes another landmark true. */
    if (out_flipped_ranks.empty() && out_kept_ranks.empty())
    {
        (true_landmark_atom_indices.empty() ? out_kept_ranks : out_flipped_ranks).push_back(get_bot_rank());
    }
}

/**
 * LandmarkNoveltyTable
 */

bool LandmarkNoveltyTable::fits_dense(size_t num_ranks, size_t num_atoms, size_t arity, const LandmarkNoveltyTableOptions& options)
{
    /* Representational limit first, because `force_dense` cannot make an unaddressable table
       addressable: free tuple indices are `TupleIndex`-wide. Computed by repeated multiplication
       with a saturation check rather than `pow`, so an over-large stride is detected instead of
       silently wrapping. */
    constexpr auto max_stride = size_t(std::numeric_limits<TupleIndex>::max());
    auto stride = size_t(1);
    for (size_t i = 0; i < arity; ++i)
    {
        if (stride > max_stride / (num_atoms + 1))
        {
            return false;
        }
        stride *= (num_atoms + 1);
    }

    if (options.force_dense)
    {
        return true;
    }
    if (options.max_dense_table_bytes == 0 || num_ranks == 0)
    {
        return false;
    }

    // One bit per cell.
    const auto max_cells =
        (options.max_dense_table_bytes > std::numeric_limits<size_t>::max() / 8) ? std::numeric_limits<size_t>::max() : options.max_dense_table_bytes * 8;
    return stride <= max_cells / num_ranks;
}

LandmarkNoveltyTable::LandmarkNoveltyTable(AtomIndexList landmark_atom_indices, size_t arity, LandmarkNoveltyTableOptions options) :
    LandmarkNoveltyTable(std::move(landmark_atom_indices), arity, 0, options)
{
}

LandmarkNoveltyTable::LandmarkNoveltyTable(AtomIndexList landmark_atom_indices, size_t arity, size_t num_atoms, LandmarkNoveltyTableOptions options) :
    m_coordinates(std::move(landmark_atom_indices)),
    m_options(options),
    m_tuple_index_mapper(arity, num_atoms),
    m_table(),
    m_state_tuple_index_generator(&m_tuple_index_mapper),
    m_state_pair_tuple_index_generator(&m_tuple_index_mapper)
{
    if (fits_dense(m_coordinates.get_num_ranks(), num_atoms, arity, m_options))
    {
        m_table = DenseTable(m_coordinates.get_num_ranks() * get_stride(), false);
    }
    else
    {
        m_table = SparseTable {};
    }
}

size_t LandmarkNoveltyTable::get_table_size() const
{
    return std::visit([](const auto& table) { return table.size(); }, m_table);
}

void LandmarkNoveltyTable::resize_to_fit(AtomIndex atom_index)
{
    if (atom_index < m_tuple_index_mapper.get_num_atoms())
    {
        return;
    }

    const auto arity = m_tuple_index_mapper.get_arity();
    const auto new_num_atoms = grown_num_atoms(m_tuple_index_mapper.get_num_atoms(), atom_index);
    const auto new_placeholder = new_num_atoms;
    const auto num_ranks = m_coordinates.get_num_ranks();

    const auto old_tuple_index_mapper = m_tuple_index_mapper;
    const auto old_stride = get_stride();

    m_tuple_index_mapper.initialize(arity, new_num_atoms);

    /* The atom universe only grows, so a table that no longer fits its budget never fits again:
       the dense -> sparse switch below is one-way. */
    const auto target_dense = fits_dense(num_ranks, new_num_atoms, arity, m_options);

    auto atom_indices = AtomIndexList {};
    atom_indices.reserve(arity);
    const auto remap = [&](TupleIndex tuple_index)
    {
        old_tuple_index_mapper.to_atom_indices(tuple_index, atom_indices);
        for (size_t i = atom_indices.size(); i < arity; ++i)
        {
            atom_indices.push_back(new_placeholder);
        }
        return m_tuple_index_mapper.to_tuple_index(atom_indices);
    };

    if (auto* dense = std::get_if<DenseTable>(&m_table))
    {
        /* The landmark rank is the high digit, so only the free-tuple digits are remapped.
           Iterating the free tuple in the outer loop keeps the number of (comparatively expensive)
           index conversions at one per free tuple instead of one per (rank, free tuple) pair. */
        const auto new_stride = target_dense ? get_stride() : 0;
        auto new_dense = target_dense ? DenseTable(num_ranks * new_stride, false) : DenseTable {};
        auto new_sparse = SparseTable {};

        for (TupleIndex tuple_index = 0; tuple_index < old_stride; ++tuple_index)
        {
            auto any_rank_set = false;
            for (size_t rank = 0; rank < num_ranks; ++rank)
            {
                if ((*dense)[rank * old_stride + tuple_index])
                {
                    any_rank_set = true;
                    break;
                }
            }
            if (!any_rank_set)
            {
                continue;
            }

            const auto new_tuple_index = remap(tuple_index);
            for (size_t rank = 0; rank < num_ranks; ++rank)
            {
                if ((*dense)[rank * old_stride + tuple_index])
                {
                    if (target_dense)
                    {
                        new_dense[rank * new_stride + new_tuple_index] = true;
                    }
                    else
                    {
                        new_sparse.insert(make_landmark_tuple_key(static_cast<uint32_t>(rank), new_tuple_index));
                    }
                }
            }
        }

        if (target_dense)
        {
            m_table = std::move(new_dense);
        }
        else
        {
            m_table = std::move(new_sparse);
        }
        return;
    }

    /* Sparse: only the live entries are rewritten, and only their low half. Two old tuples
       collapsing onto one new index simply dedupe in the set. */
    auto& sparse = std::get<SparseTable>(m_table);
    auto new_sparse = SparseTable {};
    new_sparse.reserve(sparse.size());
    for (const auto key : sparse)
    {
        const auto rank = static_cast<uint32_t>(key >> 32);
        const auto tuple_index = static_cast<TupleIndex>(key & 0xFFFFFFFFull);
        new_sparse.insert(make_landmark_tuple_key(rank, remap(tuple_index)));
    }
    m_table = std::move(new_sparse);
}

void LandmarkNoveltyTable::resize_to_fit(const State& state)
{
    /* Tables are normally created over the whole fluent atom universe (see `iw.cpp`), so every one
       of these calls -- one per state and one per successor, on every query -- exists only to
       discover it has nothing to do. Scanning for the maximum costs a step per atom in the state,
       which showed up as a measurable share of a landmark search's time. */
    if (!has_fluent_atom_at_or_beyond(state, m_tuple_index_mapper.get_num_atoms()))
    {
        return;
    }

    if (const auto atom_index = max_fluent_atom_index(state))
    {
        resize_to_fit(*atom_index);
    }
}

void LandmarkNoveltyTable::fill_scratch_with_state_tuples(const State& state)
{
    m_scratch_tuples.clear();

    if (state.get_atoms<FluentTag>().count() + 1 < m_tuple_index_mapper.get_arity())
    {
        return;
    }

    if (m_tuple_index_mapper.get_arity() == 1)
    {
        /* `to_tuple_index({a}) == a` at arity 1 (`m_factors[0] == 1`) and the all-placeholder tuple
           is `num_atoms`, so the generator would spend its whole iteration protocol reproducing the
           atom indices it was handed. See `fill_scratch_with_delta_transition_tuples`. */
        for (const auto atom_index : state.get_atoms<FluentTag>())
        {
            m_scratch_tuples.push_back(static_cast<TupleIndex>(atom_index));
        }
        m_scratch_tuples.push_back(static_cast<TupleIndex>(m_tuple_index_mapper.get_num_atoms()));
        return;
    }

    for (auto it = m_state_tuple_index_generator.begin(state); it != m_state_tuple_index_generator.end(); ++it)
    {
        m_scratch_tuples.push_back(*it);
    }
}

void LandmarkNoveltyTable::fill_scratch_with_transition_tuples(const State& state, const State& succ_state)
{
    m_scratch_tuples.clear();

    if (succ_state.get_atoms<FluentTag>().count() + 1 < m_tuple_index_mapper.get_arity())
    {
        return;
    }

    if (m_tuple_index_mapper.get_arity() == 1)
    {
        /* The tuples of the transition containing an added atom are the singletons of
           `atoms(succ) \ atoms(state)`, whose tuple indices are the atom indices themselves. The
           empty tuple contains no added atom and is correctly absent. */
        const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
        for (const auto atom_index : succ_state.get_atoms<FluentTag>())
        {
            if (!state_fluent_atoms.get(atom_index))
            {
                m_scratch_tuples.push_back(static_cast<TupleIndex>(atom_index));
            }
        }
        return;
    }

    for (auto it = m_state_pair_tuple_index_generator.begin(state, succ_state); it != m_state_pair_tuple_index_generator.end(); ++it)
    {
        m_scratch_tuples.push_back(*it);
    }
}

bool LandmarkNoveltyTable::visit_scratch_tuples(const std::vector<uint32_t>& ranks, bool update)
{
    auto novel = false;

    if (auto* dense = std::get_if<DenseTable>(&m_table))
    {
        const auto stride = get_stride();
        for (const auto rank : ranks)
        {
            const auto base = size_t(rank) * stride;
            for (const auto tuple_index : m_scratch_tuples)
            {
                const auto index = base + tuple_index;
                assert(index < dense->size());

                if (!(*dense)[index])
                {
                    novel = true;
                    if (!update)
                    {
                        return true;
                    }
                    (*dense)[index] = true;
                }
            }
        }
        return novel;
    }

    auto& sparse = std::get<SparseTable>(m_table);
    for (const auto rank : ranks)
    {
        for (const auto tuple_index : m_scratch_tuples)
        {
            const auto key = make_landmark_tuple_key(rank, tuple_index);
            if (update)
            {
                novel = sparse.insert(key).second || novel;
            }
            else if (!sparse.contains(key))
            {
                return true;
            }
        }
    }
    return novel;
}

void LandmarkNoveltyTable::fill_scratch_with_delta_successor_tuples(const State& state,
                                                                    const AtomIndexList& add_atom_indices,
                                                                    const AtomIndexList& del_atom_indices)
{
    m_scratch_tuples.clear();

    if (m_tuple_index_mapper.get_arity() == 1)
    {
        /* At arity 1 the successor's tuple indices are its atom indices, so the merge can write the
           answer straight into the scratch tuples: no second list and no generator pass. This is
           the hot path -- an action that flips a landmark on reaches it for every transition. */
        auto it_add_fast = add_atom_indices.begin();
        auto it_del_fast = del_atom_indices.begin();
        for (const auto atom_index : state.get_atoms<FluentTag>())
        {
            for (; (it_add_fast != add_atom_indices.end()) && (*it_add_fast < atom_index); ++it_add_fast)
            {
                m_scratch_tuples.push_back(static_cast<TupleIndex>(*it_add_fast));
            }
            for (; (it_del_fast != del_atom_indices.end()) && (*it_del_fast < atom_index); ++it_del_fast)
            {
            }
            if ((it_del_fast != del_atom_indices.end()) && (*it_del_fast == atom_index))
            {
                ++it_del_fast;
                continue;
            }
            m_scratch_tuples.push_back(static_cast<TupleIndex>(atom_index));
        }
        for (; it_add_fast != add_atom_indices.end(); ++it_add_fast)
        {
            m_scratch_tuples.push_back(static_cast<TupleIndex>(*it_add_fast));
        }
        m_scratch_tuples.push_back(static_cast<TupleIndex>(m_tuple_index_mapper.get_num_atoms()));
        return;
    }

    /* `atoms(s') = (atoms(s) \ del) | add`, merged in ascending order because the tuple generator
       requires a sorted list. All three inputs are already ascending. */
    m_scratch_delta_successor_atoms.clear();
    auto it_add = add_atom_indices.begin();
    auto it_del = del_atom_indices.begin();
    for (const auto atom_index : state.get_atoms<FluentTag>())
    {
        for (; (it_add != add_atom_indices.end()) && (*it_add < atom_index); ++it_add)
        {
            m_scratch_delta_successor_atoms.push_back(*it_add);
        }
        for (; (it_del != del_atom_indices.end()) && (*it_del < atom_index); ++it_del)
        {
        }
        if ((it_del != del_atom_indices.end()) && (*it_del == atom_index))
        {
            ++it_del;
            continue;
        }
        m_scratch_delta_successor_atoms.push_back(atom_index);
    }
    for (; it_add != add_atom_indices.end(); ++it_add)
    {
        m_scratch_delta_successor_atoms.push_back(*it_add);
    }

    if (m_scratch_delta_successor_atoms.size() + 1 < m_tuple_index_mapper.get_arity())
    {
        return;
    }

    /* `StateTupleIndexGenerator::begin(const State&)` appends the placeholder itself; the
       atom-list overload does not, so the empty tuple would be missing without this. */
    m_scratch_delta_successor_atoms.push_back(m_tuple_index_mapper.get_num_atoms());

    for (auto it = m_state_tuple_index_generator.begin(m_scratch_delta_successor_atoms); it != m_state_tuple_index_generator.end(); ++it)
    {
        m_scratch_tuples.push_back(*it);
    }
}

void LandmarkNoveltyTable::fill_scratch_with_delta_transition_tuples(const State& state,
                                                                     const AtomIndexList& add_atom_indices,
                                                                     const AtomIndexList& del_atom_indices)
{
    m_scratch_tuples.clear();

    if (add_atom_indices.empty())
    {
        /* No tuple of the transition contains an added atom, so this half is empty. */
        return;
    }

    if (m_tuple_index_mapper.get_arity() == 1)
    {
        /* The tuples containing an added atom are exactly the singletons of the added atoms, and at
           arity 1 the tuple index of `{a}` is `a`. Taking the shortcut is the whole point of the
           fast path: the general branch below has to materialize `atoms(s) \ del` only to hand the
           generator a list it never reads at this arity. */
        m_scratch_tuples.assign(add_atom_indices.begin(), add_atom_indices.end());
        return;
    }

    m_scratch_delta_kept_atoms.clear();
    auto it_del = del_atom_indices.begin();
    for (const auto atom_index : state.get_atoms<FluentTag>())
    {
        for (; (it_del != del_atom_indices.end()) && (*it_del < atom_index); ++it_del)
        {
        }
        if ((it_del != del_atom_indices.end()) && (*it_del == atom_index))
        {
            ++it_del;
            continue;
        }
        m_scratch_delta_kept_atoms.push_back(atom_index);
    }

    if (m_scratch_delta_kept_atoms.size() + add_atom_indices.size() + 1 < m_tuple_index_mapper.get_arity())
    {
        return;
    }

    for (auto it = m_state_pair_tuple_index_generator.begin(m_scratch_delta_kept_atoms, add_atom_indices);
         it != m_state_pair_tuple_index_generator.end();
         ++it)
    {
        m_scratch_tuples.push_back(*it);
    }
}

bool LandmarkNoveltyTable::contains_pair(uint32_t rank, TupleIndex tuple_index) const
{
    if (const auto* dense = std::get_if<DenseTable>(&m_table))
    {
        const auto index = size_t(rank) * get_stride() + tuple_index;
        assert(index < dense->size());
        return (*dense)[index];
    }
    return std::get<SparseTable>(m_table).contains(make_landmark_tuple_key(rank, tuple_index));
}

bool LandmarkNoveltyTable::test_novelty_and_update_table(const State& state)
{
    resize_to_fit(state);

    m_coordinates.collect(state, m_scratch_kept_ranks);
    fill_scratch_with_state_tuples(state);

    return visit_scratch_tuples(m_scratch_kept_ranks, true);
}

bool LandmarkNoveltyTable::test_novelty_and_update_table(const State& state, const State& succ_state)
{
    resize_to_fit(state);
    resize_to_fit(succ_state);

    m_coordinates.collect_transition(state, succ_state, m_scratch_flipped_ranks, m_scratch_kept_ranks);

    auto novel = false;
    if (!m_scratch_flipped_ranks.empty())
    {
        fill_scratch_with_state_tuples(succ_state);
        novel = visit_scratch_tuples(m_scratch_flipped_ranks, true) || novel;
    }
    if (!m_scratch_kept_ranks.empty())
    {
        fill_scratch_with_transition_tuples(state, succ_state);
        novel = visit_scratch_tuples(m_scratch_kept_ranks, true) || novel;
    }
    return novel;
}

bool LandmarkNoveltyTable::test_novelty_read_only(const State& state)
{
    resize_to_fit(state);

    m_coordinates.collect(state, m_scratch_kept_ranks);
    fill_scratch_with_state_tuples(state);

    return visit_scratch_tuples(m_scratch_kept_ranks, false);
}

bool LandmarkNoveltyTable::test_novelty_read_only(const State& state, const State& succ_state)
{
    resize_to_fit(state);
    resize_to_fit(succ_state);

    m_coordinates.collect_transition(state, succ_state, m_scratch_flipped_ranks, m_scratch_kept_ranks);

    if (!m_scratch_flipped_ranks.empty())
    {
        fill_scratch_with_state_tuples(succ_state);
        if (visit_scratch_tuples(m_scratch_flipped_ranks, false))
        {
            return true;
        }
    }
    if (!m_scratch_kept_ranks.empty())
    {
        fill_scratch_with_transition_tuples(state, succ_state);
        if (visit_scratch_tuples(m_scratch_kept_ranks, false))
        {
            return true;
        }
    }
    return false;
}

void LandmarkNoveltyTable::refresh_delta_query_state(const State& state)
{
    if (m_delta_query_state_index.has_value() && (*m_delta_query_state_index == state.get_index()))
    {
        return;
    }

    resize_to_fit(state);
    m_coordinates.collect_true_landmark_atoms(state, m_delta_query_true_landmark_atoms);
    m_delta_query_state_index = state.get_index();
}

bool LandmarkNoveltyTable::test_novelty_read_only_from_delta(const State& state,
                                                             const AtomIndexList& add_atom_indices,
                                                             const AtomIndexList& del_atom_indices)
{
    refresh_delta_query_state(state);

    if (!add_atom_indices.empty())
    {
        /* The successor's atoms are a subset of `atoms(state) | add`, so widening for the largest
           added index covers every tuple this query can generate. A resize renumbers free tuple
           indices but not landmark ranks, so the cached `L(state)` survives it. */
        resize_to_fit(add_atom_indices.back());
    }

    m_coordinates.collect_transition_from_delta(m_delta_query_true_landmark_atoms,
                                                add_atom_indices,
                                                del_atom_indices,
                                                m_scratch_flipped_ranks,
                                                m_scratch_kept_ranks);

    /* Order matters for cost, not for the answer: the kept half never reconstructs the successor,
       so running it first keeps the common no-flip transition off the expensive branch entirely. */
    if (!m_scratch_kept_ranks.empty())
    {
        fill_scratch_with_delta_transition_tuples(state, add_atom_indices, del_atom_indices);
        if (visit_scratch_tuples(m_scratch_kept_ranks, false))
        {
            return true;
        }
    }
    if (!m_scratch_flipped_ranks.empty())
    {
        fill_scratch_with_delta_successor_tuples(state, add_atom_indices, del_atom_indices);
        if (visit_scratch_tuples(m_scratch_flipped_ranks, false))
        {
            return true;
        }
    }
    return false;
}

void LandmarkNoveltyTable::compute_transition_novel_fluent_atom_indices_read_only(const State& state,
                                                                                  const State& succ_state,
                                                                                  AtomIndexList& out_novel_fluent_atom_indices)
{
    if (m_tuple_index_mapper.get_arity() != 1)
    {
        throw std::invalid_argument("LandmarkNoveltyTable::compute_transition_novel_fluent_atom_indices_read_only only supports arity 1.");
    }

    out_novel_fluent_atom_indices.clear();

    resize_to_fit(state);
    resize_to_fit(succ_state);

    m_coordinates.collect_transition(state, succ_state, m_scratch_flipped_ranks, m_scratch_kept_ranks);

    const auto& state_fluent_atoms = state.get_atoms<FluentTag>();
    for (const auto atom_index : succ_state.get_atoms<FluentTag>())
    {
        /* At arity 1 the tuple index of the singleton `{atom_index}` is the atom index itself. */
        const auto tuple_index = static_cast<TupleIndex>(atom_index);

        auto is_novel = false;
        for (const auto rank : m_scratch_flipped_ranks)
        {
            if (!contains_pair(rank, tuple_index))
            {
                is_novel = true;
                break;
            }
        }
        /* A kept coordinate only ever pairs with atoms the transition adds; an already-true atom
           under an already-true coordinate was marked when the predecessor was processed. */
        if (!is_novel && !state_fluent_atoms.get(atom_index))
        {
            for (const auto rank : m_scratch_kept_ranks)
            {
                if (!contains_pair(rank, tuple_index))
                {
                    is_novel = true;
                    break;
                }
            }
        }

        if (is_novel)
        {
            out_novel_fluent_atom_indices.push_back(atom_index);
        }
    }
}

/**
 * LandmarkMinimumGNoveltyTable
 */

LandmarkMinimumGNoveltyTable::LandmarkMinimumGNoveltyTable(AtomIndexList landmark_atom_indices, size_t arity) :
    LandmarkMinimumGNoveltyTable(std::move(landmark_atom_indices), arity, 0)
{
}

LandmarkMinimumGNoveltyTable::LandmarkMinimumGNoveltyTable(AtomIndexList landmark_atom_indices, size_t arity, size_t num_atoms) :
    m_coordinates(std::move(landmark_atom_indices)),
    m_tuple_index_mapper(arity, num_atoms),
    m_minimum_g_values(),
    m_state_tuple_index_generator(&m_tuple_index_mapper),
    m_state_pair_tuple_index_generator(&m_tuple_index_mapper)
{
}

void LandmarkMinimumGNoveltyTable::resize_to_fit(AtomIndex atom_index)
{
    if (atom_index < m_tuple_index_mapper.get_num_atoms())
    {
        return;
    }

    const auto arity = m_tuple_index_mapper.get_arity();
    const auto new_size = grown_num_atoms(m_tuple_index_mapper.get_num_atoms(), atom_index);
    const auto new_placeholder = new_size;

    const auto old_tuple_index_mapper = m_tuple_index_mapper;
    m_tuple_index_mapper.initialize(arity, new_size);

    /* Only the live entries are rewritten, not the whole tuple space, and only the low half of
       each key changes. Where two old tuples collapse onto one new index, the smaller label wins,
       matching `MinimumGNoveltyTable::resize_to_fit`. */
    auto remapped = absl::flat_hash_map<uint64_t, ContinuousCost> {};
    remapped.reserve(m_minimum_g_values.size());

    auto atom_indices = AtomIndexList {};
    atom_indices.reserve(arity);

    for (const auto& [key, g_value] : m_minimum_g_values)
    {
        const auto rank = static_cast<uint32_t>(key >> 32);
        const auto tuple_index = static_cast<TupleIndex>(key & 0xFFFFFFFFull);

        old_tuple_index_mapper.to_atom_indices(tuple_index, atom_indices);
        for (size_t i = atom_indices.size(); i < arity; ++i)
        {
            atom_indices.push_back(new_placeholder);
        }
        const auto new_key = make_landmark_tuple_key(rank, m_tuple_index_mapper.to_tuple_index(atom_indices));

        const auto [it, inserted] = remapped.try_emplace(new_key, g_value);
        if (!inserted && g_value < it->second)
        {
            it->second = g_value;
        }
    }

    m_minimum_g_values = std::move(remapped);
}

void LandmarkMinimumGNoveltyTable::resize_to_fit(const State& state)
{
    /* Same cheap "already covered?" test as `LandmarkNoveltyTable::resize_to_fit`. */
    if (!has_fluent_atom_at_or_beyond(state, m_tuple_index_mapper.get_num_atoms()))
    {
        return;
    }

    if (const auto atom_index = max_fluent_atom_index(state))
    {
        resize_to_fit(*atom_index);
    }
}

void LandmarkMinimumGNoveltyTable::fill_scratch_with_state_tuples(const State& state)
{
    m_scratch_tuples.clear();

    if (state.get_atoms<FluentTag>().count() + 1 < m_tuple_index_mapper.get_arity())
    {
        return;
    }

    for (auto it = m_state_tuple_index_generator.begin(state); it != m_state_tuple_index_generator.end(); ++it)
    {
        m_scratch_tuples.push_back(*it);
    }
}

void LandmarkMinimumGNoveltyTable::fill_scratch_with_transition_tuples(const State& state, const State& succ_state)
{
    m_scratch_tuples.clear();

    if (succ_state.get_atoms<FluentTag>().count() + 1 < m_tuple_index_mapper.get_arity())
    {
        return;
    }

    for (auto it = m_state_pair_tuple_index_generator.begin(state, succ_state); it != m_state_pair_tuple_index_generator.end(); ++it)
    {
        m_scratch_tuples.push_back(*it);
    }
}

bool LandmarkMinimumGNoveltyTable::lower_scratch_tuples(const std::vector<uint32_t>& ranks, ContinuousCost g_value)
{
    auto improved = false;
    for (const auto rank : ranks)
    {
        for (const auto tuple_index : m_scratch_tuples)
        {
            const auto [it, inserted] = m_minimum_g_values.try_emplace(make_landmark_tuple_key(rank, tuple_index), g_value);
            if (inserted)
            {
                improved = true;
            }
            else if (g_value < it->second)
            {
                it->second = g_value;
                improved = true;
                m_lowered_existing_label = true;
            }
        }
    }
    return improved;
}

bool LandmarkMinimumGNoveltyTable::scratch_tuples_contain_g(const std::vector<uint32_t>& ranks, ContinuousCost g_value) const
{
    for (const auto rank : ranks)
    {
        for (const auto tuple_index : m_scratch_tuples)
        {
            const auto it = m_minimum_g_values.find(make_landmark_tuple_key(rank, tuple_index));
            if (it != m_minimum_g_values.end() && it->second == g_value)
            {
                return true;
            }
        }
    }
    return false;
}

bool LandmarkMinimumGNoveltyTable::test_novelty_and_update_table(const State& state, ContinuousCost g_value)
{
    resize_to_fit(state);

    m_coordinates.collect(state, m_scratch_kept_ranks);
    fill_scratch_with_state_tuples(state);

    return lower_scratch_tuples(m_scratch_kept_ranks, g_value);
}

bool LandmarkMinimumGNoveltyTable::test_novelty_and_update_table(const State& state, const State& succ_state, ContinuousCost g_value)
{
    resize_to_fit(state);
    resize_to_fit(succ_state);

    m_coordinates.collect_transition(state, succ_state, m_scratch_flipped_ranks, m_scratch_kept_ranks);

    auto improved = false;
    if (!m_scratch_flipped_ranks.empty())
    {
        fill_scratch_with_state_tuples(succ_state);
        improved = lower_scratch_tuples(m_scratch_flipped_ranks, g_value) || improved;
    }
    if (!m_scratch_kept_ranks.empty())
    {
        fill_scratch_with_transition_tuples(state, succ_state);
        improved = lower_scratch_tuples(m_scratch_kept_ranks, g_value) || improved;
    }
    return improved;
}

bool LandmarkMinimumGNoveltyTable::scratch_tuples_would_lower(const std::vector<uint32_t>& ranks, ContinuousCost g_value) const
{
    for (const auto rank : ranks)
    {
        for (const auto tuple_index : m_scratch_tuples)
        {
            const auto it = m_minimum_g_values.find(make_landmark_tuple_key(rank, tuple_index));
            if (it == m_minimum_g_values.end() || g_value < it->second)
            {
                return true;
            }
        }
    }
    return false;
}

bool LandmarkMinimumGNoveltyTable::test_would_improve(const State& state, const State& succ_state, ContinuousCost g_value)
{
    resize_to_fit(state);
    resize_to_fit(succ_state);

    m_coordinates.collect_transition(state, succ_state, m_scratch_flipped_ranks, m_scratch_kept_ranks);

    if (!m_scratch_flipped_ranks.empty())
    {
        fill_scratch_with_state_tuples(succ_state);
        if (scratch_tuples_would_lower(m_scratch_flipped_ranks, g_value))
        {
            return true;
        }
    }
    if (!m_scratch_kept_ranks.empty())
    {
        fill_scratch_with_transition_tuples(state, succ_state);
        if (scratch_tuples_would_lower(m_scratch_kept_ranks, g_value))
        {
            return true;
        }
    }
    return false;
}

bool LandmarkMinimumGNoveltyTable::test_novelty_at_g_read_only(const State& state, ContinuousCost g_value)
{
    resize_to_fit(state);

    m_coordinates.collect(state, m_scratch_kept_ranks);
    fill_scratch_with_state_tuples(state);

    return scratch_tuples_contain_g(m_scratch_kept_ranks, g_value);
}

}
