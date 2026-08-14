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

#ifndef MIMIR_SEARCH_ALGORITHMS_IW_NOVELTY_TABLE_HPP_
#define MIMIR_SEARCH_ALGORITHMS_IW_NOVELTY_TABLE_HPP_

#include "mimir/search/algorithms/iw/tuple_index_generators.hpp"
#include "mimir/search/algorithms/iw/tuple_index_mapper.hpp"
#include "mimir/search/declarations.hpp"

#include <absl/container/flat_hash_map.h>
#include <concepts>
#include <cstdint>
#include <optional>
#include <variant>

namespace mimir::search::iw
{

/// @brief `DynamicNoveltyTable` encapsulates a table to test novelty of tuples of atoms of size at most arity.
/// It automatically resizes when the atoms do not fit into the table anymore.
/// When the table resizes, tuple indices are remapped to take into account the higher number of atoms.
class DynamicNoveltyTable
{
private:
    TupleIndexMapper m_tuple_index_mapper;

    std::vector<bool> m_table;

    void resize_to_fit(AtomIndex atom_index);
    void resize_to_fit(const State& state);

    // Preallocated memory that will be modified.
    StateTupleIndexGenerator m_state_tuple_index_generator;
    StatePairTupleIndexGenerator m_state_pair_tuple_index_generator;

public:
    explicit DynamicNoveltyTable(size_t arity);
    DynamicNoveltyTable(size_t arity, size_t num_atoms);

    void compute_novel_tuples(const State& state, std::vector<AtomIndexList>& out_novel_tuples);
    void compute_novel_tuples(const State& state, const State& succ_state, std::vector<AtomIndexList>& out_novel_tuples);
    void compute_novel_tuples(const State& state, const AtomIndexList& succ_state_atom_indices, std::vector<AtomIndexList>& out_novel_tuples);

    void insert_tuples(const std::vector<AtomIndexList>& tuples);

    bool test_novelty(const State& state);
    bool test_novelty(const State& state, const State& succ_state);
    bool test_novelty(const State& state, const AtomIndexList& succ_state_atom_indices);
    bool test_novelty_read_only(const State& state) const;
    bool test_novelty_read_only(const State& state, const State& succ_state) const;
    bool test_novelty_read_only(const State& state, const AtomIndexList& succ_state_atom_indices) const;
    bool test_atom_novelty_read_only(AtomIndex atom_index) const;
    bool test_novelty_and_update_table(const State& state);

    bool test_novelty_and_update_table(const State& state, const State& succ_state);
    bool test_novelty_and_update_table(const State& state, const AtomIndexList& succ_state_atom_indices);

    void reset();

    const TupleIndexMapper& get_tuple_index_mapper() const;
};

/// @brief Stores the smallest path cost at which each fluent tuple was generated.
///
/// This table has the same tuple semantics and dynamic atom-capacity behavior as
/// `DynamicNoveltyTable`, but supports decreasing tuple labels. It is used by
/// best-first width searches whose generation order is not monotone in depth.
class MinimumGNoveltyTable
{
private:
    /// The per-tuple label is stored as a rank into `m_g_values` rather than as the cost
    /// itself. A width search visits very few distinct g values, so a rank fits in a byte
    /// almost always, and this table is the hottest randomly-accessed structure in the
    /// search: one `ContinuousCost` per tuple would be eight times the cache footprint of
    /// `DynamicNoveltyTable`'s bit per tuple, on a table that grows as num_atoms^arity.
    /// The rank width is promoted on demand, so arbitrary cost values remain exact.
    using Ranks8 = std::vector<uint8_t>;
    using Ranks16 = std::vector<uint16_t>;
    using Ranks32 = std::vector<uint32_t>;
    using Ranks = std::variant<Ranks8, Ranks16, Ranks32>;

    TupleIndexMapper m_tuple_index_mapper;
    Ranks m_minimum_g_ranks;
    /// Rank -> cost. Tiny and cache resident; indexed only for tuples already labelled.
    std::vector<ContinuousCost> m_g_values;
    absl::flat_hash_map<ContinuousCost, uint32_t> m_g_value_to_rank;
    /// Set once some already-finite label is lowered again. While this stays false every
    /// state that ever passed the novelty test still owns a tuple at its own g value.
    bool m_lowered_existing_label = false;

    StateTupleIndexGenerator m_state_tuple_index_generator;
    StatePairTupleIndexGenerator m_state_pair_tuple_index_generator;

    /// @brief Index of `g_value` in `m_g_values`, inserting it (and widening the rank type
    /// if the new rank no longer fits) when it is seen for the first time.
    uint32_t get_or_create_rank(ContinuousCost g_value);
    /// @brief Rank of `g_value`, or `std::nullopt` when it has never been recorded.
    std::optional<uint32_t> find_rank(ContinuousCost g_value) const;
    void widen_ranks_to_fit(uint32_t rank);

    void resize_to_fit(AtomIndex atom_index);
    void resize_to_fit(const State& state);

    template<typename Generator, typename... Args>
    bool update_from(Generator& generator, ContinuousCost g_value, Args&&... args);

public:
    explicit MinimumGNoveltyTable(size_t arity);
    MinimumGNoveltyTable(size_t arity, size_t num_atoms);
    MinimumGNoveltyTable(const MinimumGNoveltyTable&) = delete;
    MinimumGNoveltyTable& operator=(const MinimumGNoveltyTable&) = delete;
    MinimumGNoveltyTable(MinimumGNoveltyTable&&) = delete;
    MinimumGNoveltyTable& operator=(MinimumGNoveltyTable&&) = delete;

    /// @brief Lower the labels of all tuples in `state` to `g_value` where possible.
    /// @return true iff at least one tuple label was lowered.
    bool test_novelty_and_update_table(const State& state, ContinuousCost g_value);

    /// @brief Lower labels of successor tuples containing at least one added atom.
    /// @return true iff at least one tuple label was lowered.
    bool test_novelty_and_update_table(const State& state, const State& succ_state, ContinuousCost g_value);

    /// @brief Test whether `state` contains a tuple whose current minimum label equals `g_value`.
    bool test_novelty_at_g_read_only(const State& state, ContinuousCost g_value);

    /// @brief Whether any already-labelled tuple was ever lowered again. False means no
    /// state can have had its label stolen, so the stale-novelty test cannot fail.
    bool has_lowered_existing_label() const { return m_lowered_existing_label; }

    const TupleIndexMapper& get_tuple_index_mapper() const;
};

}

#endif
