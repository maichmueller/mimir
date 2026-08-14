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

#ifndef MIMIR_SEARCH_ALGORITHMS_IW_LANDMARK_NOVELTY_TABLE_HPP_
#define MIMIR_SEARCH_ALGORITHMS_IW_LANDMARK_NOVELTY_TABLE_HPP_

#include "mimir/search/algorithms/iw/tuple_index_generators.hpp"
#include "mimir/search/algorithms/iw/tuple_index_mapper.hpp"
#include "mimir/search/declarations.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <cstdint>
#include <limits>
#include <variant>
#include <vector>

namespace mimir::search::iw
{

/// @brief The landmark coordinate of a landmark-restricted novelty feature.
///
/// A state's coordinates are the ranks of the landmark atoms true in it, or the single sentinel
/// rank `BOT` when it holds none. Keeping the coordinate *total* this way is what lets the usual
/// IW completeness argument carry over to LIW(k) (see `LandmarkNoveltyTable`), and it makes an
/// empty landmark set collapse the whole feature family back onto plain IW(k).
///
/// The landmark set is fixed at construction and never grows: landmark atoms are all known when
/// the graph is built, so only the free-tuple side of a table ever has to be resized.
class LandmarkCoordinates
{
public:
    static constexpr uint32_t NOT_A_LANDMARK = std::numeric_limits<uint32_t>::max();

    explicit LandmarkCoordinates(AtomIndexList landmark_atom_indices);

    /// @brief Rank of `atom_index`, or `NOT_A_LANDMARK` when it is not a landmark.
    uint32_t get_rank(AtomIndex atom_index) const;
    /// @brief The sentinel rank used by states holding no landmark.
    uint32_t get_bot_rank() const { return static_cast<uint32_t>(m_landmark_atom_indices.size()); }
    /// @brief Number of distinct coordinate values, i.e. `|L| + 1`.
    size_t get_num_ranks() const { return m_landmark_atom_indices.size() + 1; }
    size_t get_num_landmarks() const { return m_landmark_atom_indices.size(); }
    const AtomIndexList& get_landmark_atom_indices() const { return m_landmark_atom_indices; }

    /// @brief The coordinates of `state`: the ranks of its true landmarks, or `{BOT}`.
    void collect(const State& state, std::vector<uint32_t>& out_ranks) const;

    /// @brief Split the successor's coordinates into those that just flipped on and those that
    /// were already true in the predecessor.
    ///
    /// A feature `(l, t)` is new relative to the predecessor exactly when its landmark coordinate
    /// flipped on or its free tuple gained an atom, so the two groups pair with different tuple
    /// sets: flipped coordinates with *all* free tuples of the successor, kept ones only with
    /// tuples containing an added atom. `BOT` participates as a virtual landmark that is true
    /// exactly when no real landmark is, and flips like any other.
    void collect_transition(const State& state, const State& succ_state, std::vector<uint32_t>& out_flipped_ranks, std::vector<uint32_t>& out_kept_ranks) const;

private:
    AtomIndexList m_landmark_atom_indices;
    /// atom index -> rank, sized to the largest landmark atom index.
    std::vector<uint32_t> m_rank_by_atom_index;
};

/// @brief Packed `(landmark rank, free tuple index)` key used by the sparse table layouts. The rank
/// occupies the high half so a resize, which only ever changes what a free tuple index means,
/// rewrites the low half alone.
inline uint64_t make_landmark_tuple_key(uint32_t rank, TupleIndex tuple_index)
{
    return (static_cast<uint64_t>(rank) << 32) | static_cast<uint64_t>(tuple_index);
}

/// @brief Storage budget for `LandmarkNoveltyTable`.
///
/// The dense layout costs one bit per `(landmark rank, free tuple)` pair whether or not the search
/// ever reaches it, and that cost is paid twice over: once zeroing the allocation, and again on
/// every resize, which rescans the whole table to remap indices. Both are O(table size) sweeps that
/// a short search cannot amortize, so past some size the sparse layout -- which only ever touches
/// live entries -- is both smaller *and* faster despite hashing every lookup.
///
/// The default budget of 256 MiB is where those sweeps stop being negligible: a table that large is
/// 2^31 cells, so each zero-fill or resize rescan is a multi-hundred-millisecond linear pass, and
/// resizes happen a logarithmic number of times as the atom universe grows. Below it the dense
/// layout wins comfortably; above it a search can spend longer sweeping the table than searching.
struct LandmarkNoveltyTableOptions
{
    /// @brief Largest dense allocation to accept, in bytes. Above it the table switches to the
    /// sparse layout. Zero forces the sparse layout unconditionally.
    size_t max_dense_table_bytes = size_t(256) * 1024 * 1024;

    /// @brief Keep the dense layout regardless of `max_dense_table_bytes`.
    ///
    /// This overrides the *budget*, not the representational limit: free tuple indices are
    /// `TupleIndex`-wide, so a table whose stride cannot be addressed still falls back to sparse.
    /// Useful to measure what the budget costs, and to pin the layout in benchmarks.
    bool force_dense = false;
};

/// @brief `LandmarkNoveltyTable` tests novelty over *landmark-restricted* tuples.
///
/// Where `DynamicNoveltyTable` tracks free atom tuples of size at most `k`, this table tracks
/// pairs `(l, t)` where `l` is a landmark coordinate of the state and `t` is a free atom tuple of
/// size at most `k`. The resulting feature family -- call it LIW(k) -- is a strict subset of
/// IW(k+1)'s tuples and a strict superset of IW(k)'s, so LIW(k) prunes less than IW(k) and more
/// than IW(k+1) while its table only grows as `|L| * num_atoms^k` rather than `num_atoms^(k+1)`.
///
/// Two properties are worth spelling out because they drive the design:
///
/// 1. The plain IW(k) tuples are *not* tracked alongside, and must not be: `t` unseen implies
///    `(l, t)` unseen for every `l`, so every state IW(k) admits this table admits too. Adding the
///    free tuples back would be pure redundancy.
///
/// 2. LIW(k) still solves every problem of width at most `k`. For any tuple `t` optimally achieved
///    by a state `s` and any coordinate `l` of `s`, the pair `(l, t)` is also optimally achieved
///    at `s` -- it cannot be achieved earlier, since it subsumes `t` -- so the usual IW argument
///    that some optimal achiever of every tracked feature is expanded carries over. This needs
///    every state to own at least one coordinate, which is exactly what `BOT` guarantees.
///
/// The table resizes on demand exactly like `DynamicNoveltyTable`. It has two layouts: a dense bit
/// array in which the landmark rank is the high digit of the index, and a sparse set of packed
/// keys. Which one is used is decided by `LandmarkNoveltyTableOptions` from the projected
/// allocation, and re-decided on every resize -- a table that outgrows its budget switches to
/// sparse mid-search, carrying its marks across. The switch is one-way: having outgrown the budget
/// once, it will only grow further.
class LandmarkNoveltyTable
{
public:
    static constexpr uint32_t NOT_A_LANDMARK = LandmarkCoordinates::NOT_A_LANDMARK;

    /// @param landmark_atom_indices the landmark atoms `L`; may be empty, in which case every
    /// state falls back to `BOT` and the table behaves exactly like `DynamicNoveltyTable(arity)`.
    /// @param arity the number of *free* coordinates `k`. LIW(k) tuples have size `k + 1`.
    LandmarkNoveltyTable(AtomIndexList landmark_atom_indices, size_t arity, LandmarkNoveltyTableOptions options = {});
    LandmarkNoveltyTable(AtomIndexList landmark_atom_indices, size_t arity, size_t num_atoms, LandmarkNoveltyTableOptions options = {});

    /// @brief Mark every landmark-restricted tuple of `state` and report whether any was unseen.
    /// Used for the initial state, where there is no predecessor to take a delta against.
    bool test_novelty_and_update_table(const State& state);

    /// @brief Mark every landmark-restricted tuple of the transition `state -> succ_state` that is
    /// not already a tuple of `state`, and report whether any was unseen.
    bool test_novelty_and_update_table(const State& state, const State& succ_state);

    /// @brief As above but without touching the table.
    bool test_novelty_read_only(const State& state);
    bool test_novelty_read_only(const State& state, const State& succ_state);

    uint32_t get_landmark_rank(AtomIndex atom_index) const { return m_coordinates.get_rank(atom_index); }
    uint32_t get_bot_rank() const { return m_coordinates.get_bot_rank(); }
    size_t get_num_landmarks() const { return m_coordinates.get_num_landmarks(); }
    const AtomIndexList& get_landmark_atom_indices() const { return m_coordinates.get_landmark_atom_indices(); }
    void collect_landmark_ranks(const State& state, std::vector<uint32_t>& out_ranks) const { m_coordinates.collect(state, out_ranks); }

    const TupleIndexMapper& get_tuple_index_mapper() const { return m_tuple_index_mapper; }
    const LandmarkNoveltyTableOptions& get_options() const { return m_options; }

    bool is_dense() const { return std::holds_alternative<DenseTable>(m_table); }
    /// @brief Dense cells when dense, live entries when sparse.
    size_t get_table_size() const;

    /// @brief Whether a dense table of `num_ranks * (num_atoms + 1)^arity` cells is within budget
    /// and addressable. Exposed so callers can predict the layout without building a table.
    static bool fits_dense(size_t num_ranks, size_t num_atoms, size_t arity, const LandmarkNoveltyTableOptions& options);

private:
    using DenseTable = std::vector<bool>;
    using SparseTable = absl::flat_hash_set<uint64_t>;

    /// Number of free-tuple indices per landmark rank; the landmark rank is the high digit.
    size_t get_stride() const { return m_tuple_index_mapper.get_max_tuple_index() + 1; }

    void resize_to_fit(AtomIndex atom_index);
    void resize_to_fit(const State& state);

    /// @brief Mark `ranks x m_scratch_tuples`, or only test it when `update` is false.
    bool visit_scratch_tuples(const std::vector<uint32_t>& ranks, bool update);

    void fill_scratch_with_state_tuples(const State& state);
    void fill_scratch_with_transition_tuples(const State& state, const State& succ_state);

    LandmarkCoordinates m_coordinates;
    LandmarkNoveltyTableOptions m_options;
    TupleIndexMapper m_tuple_index_mapper;
    std::variant<DenseTable, SparseTable> m_table;

    StateTupleIndexGenerator m_state_tuple_index_generator;
    StatePairTupleIndexGenerator m_state_pair_tuple_index_generator;

    /// The free tuples of the current state/transition, materialized once so that the loop over
    /// landmark ranks does not re-run the (comparatively expensive) tuple generator per rank.
    TupleIndexList m_scratch_tuples;
    mutable std::vector<uint32_t> m_scratch_flipped_ranks;
    mutable std::vector<uint32_t> m_scratch_kept_ranks;
};

/// @brief Landmark-restricted counterpart of `MinimumGNoveltyTable`: stores the smallest path cost
/// at which each `(landmark coordinate, free tuple)` pair was generated.
///
/// Unlike `LandmarkNoveltyTable` this table is *sparse*. `MinimumGNoveltyTable` can afford a dense
/// array because it spends one small rank per free tuple; multiplying that by `|L| + 1` and by the
/// eight bytes a cost needs would not fit, while a best-first search only ever reaches a small
/// fraction of the product. Sparsity also makes growing the atom universe cheap -- resizing walks
/// the live entries rather than the whole product -- and removes the need for the rank-widening
/// machinery that the dense table uses to keep its cells small.
class LandmarkMinimumGNoveltyTable
{
public:
    LandmarkMinimumGNoveltyTable(AtomIndexList landmark_atom_indices, size_t arity);
    LandmarkMinimumGNoveltyTable(AtomIndexList landmark_atom_indices, size_t arity, size_t num_atoms);

    /// @brief Lower the labels of all landmark-restricted tuples of `state` to `g_value`.
    /// @return true iff at least one label was lowered.
    bool test_novelty_and_update_table(const State& state, ContinuousCost g_value);

    /// @brief Lower the labels of the transition's landmark-restricted tuples to `g_value`.
    /// @return true iff at least one label was lowered.
    bool test_novelty_and_update_table(const State& state, const State& succ_state, ContinuousCost g_value);

    /// @brief Whether `state` still owns a landmark-restricted tuple labelled exactly `g_value`.
    bool test_novelty_at_g_read_only(const State& state, ContinuousCost g_value);

    /// @brief Whether any already-finite label was ever lowered again. False means no state can
    /// have had its label stolen, so the stale-novelty test cannot fail.
    bool has_lowered_existing_label() const { return m_lowered_existing_label; }

    size_t get_num_landmarks() const { return m_coordinates.get_num_landmarks(); }
    const TupleIndexMapper& get_tuple_index_mapper() const { return m_tuple_index_mapper; }
    size_t get_num_labelled_tuples() const { return m_minimum_g_values.size(); }

private:
    void resize_to_fit(AtomIndex atom_index);
    void resize_to_fit(const State& state);

    bool lower_scratch_tuples(const std::vector<uint32_t>& ranks, ContinuousCost g_value);
    bool scratch_tuples_contain_g(const std::vector<uint32_t>& ranks, ContinuousCost g_value) const;

    void fill_scratch_with_state_tuples(const State& state);
    void fill_scratch_with_transition_tuples(const State& state, const State& succ_state);

    LandmarkCoordinates m_coordinates;
    TupleIndexMapper m_tuple_index_mapper;
    absl::flat_hash_map<uint64_t, ContinuousCost> m_minimum_g_values;
    bool m_lowered_existing_label = false;

    StateTupleIndexGenerator m_state_tuple_index_generator;
    StatePairTupleIndexGenerator m_state_pair_tuple_index_generator;

    TupleIndexList m_scratch_tuples;
    mutable std::vector<uint32_t> m_scratch_flipped_ranks;
    mutable std::vector<uint32_t> m_scratch_kept_ranks;
};

}

#endif
