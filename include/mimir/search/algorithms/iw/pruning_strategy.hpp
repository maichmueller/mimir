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

#ifndef MIMIR_SEARCH_ALGORITHMS_IW_PRUNING_STRATEGY_HPP_
#define MIMIR_SEARCH_ALGORITHMS_IW_PRUNING_STRATEGY_HPP_

#include "mimir/search/algorithms/iw/landmark_novelty_table.hpp"
#include "mimir/search/algorithms/iw/novelty_table.hpp"
#include "mimir/search/algorithms/iw/tuple_index_mapper.hpp"
#include "mimir/search/algorithms/strategies/pruning_strategy.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/search/state.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include <cstdint>
#include <memory>
#include <functional>
#include <optional>
#include <tuple>
#include <unordered_set>

namespace mimir::search::iw
{
namespace astar_iw_friend
{
class AbstractedMinimumGNoveltyTable;
}

class ArityZeroNoveltyPruningStrategyImpl : public IPruningStrategy
{
private:
    State m_initial_state;

public:
    explicit ArityZeroNoveltyPruningStrategyImpl(State initial_state);

    static PruningStrategy create(State initial_state);

    bool test_prune_initial_state(const State& state) override;
    bool test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ) override;
    bool supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const override;
    bool supports_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const override;
    bool supports_relaxed_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const override;
    bool test_prune_successor_state_for_beam_selection(const State& state,
                                                       const State& succ_state,
                                                       bool is_new_succ,
                                                       BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_staged_successor_state_for_beam_selection(const State& state,
                                                              const FlatBitset& succ_fluent_atoms,
                                                              const FlatBitset& succ_derived_atoms,
                                                              const FlatDoubleList& succ_numeric_variables,
                                                              const AtomIndexList& succ_fluent_atom_indices,
                                                              bool is_new_succ,
                                                              BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_staged_successor_state_for_relaxed_beam_selection(const State& state,
                                                                      const FlatBitset& succ_fluent_atoms,
                                                                      const FlatBitset& succ_derived_atoms,
                                                                      const FlatDoubleList& succ_numeric_variables,
                                                                      const AtomIndexList& succ_fluent_atom_indices,
                                                                      BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_staged_successor_state_for_beam_replay(const State& state,
                                                           const FlatBitset& succ_fluent_atoms,
                                                           const FlatBitset& succ_derived_atoms,
                                                           const FlatDoubleList& succ_numeric_variables,
                                                           const AtomIndexList& succ_fluent_atom_indices,
                                                           bool is_new_succ,
                                                           BeamNoveltyMode beam_novelty_mode) override;
};

class ArityKNoveltyPruningStrategyImpl : public IPruningStrategy
{
private:
    struct AtomIndexListHash
    {
        size_t operator()(const AtomIndexList& atom_indices) const noexcept;
    };

    DynamicNoveltyTable m_novelty_table;
    bool m_optimize_root_depth_one_continuation;
    std::optional<Index> m_root_state_index;
    std::vector<AtomIndexList> m_beam_layer_delta_tuples;
    std::unordered_set<AtomIndexList, AtomIndexListHash> m_beam_layer_delta_tuple_set;
    std::vector<AtomIndexList> m_scratch_novel_tuples;
    AtomIndexList m_scratch_atom_indices_key;
    std::unordered_set<Index> m_skip_depth_one_expansion_state_indices;
    std::unordered_set<AtomIndexList, AtomIndexListHash> m_skip_depth_one_expansion_fluent_atom_indices_fallback;

    bool test_transition_novelty(const State& state, const State& succ_state);
    bool test_transition_novelty_and_update_delta(const State& state, const State& succ_state);

public:
    ArityKNoveltyPruningStrategyImpl(size_t arity, size_t num_atoms, bool optimize_root_depth_one_continuation = false);

    static PruningStrategy create(size_t arity, size_t num_atoms, bool optimize_root_depth_one_continuation = false);

    bool test_prune_initial_state(const State& state) override;
    bool test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ) override;
    bool supports_action_add_effect_precheck() const override;
    bool should_bypass_action_add_effect_precheck(const State& state) const override;
    bool test_transition_novelty_from_add_effects(const State& state,
                                                  const AtomIndexList& add_fluent_atom_indices,
                                                  const AtomIndexList& del_fluent_atom_indices) const override;
    bool consume_skip_state_expansion(const State& state) override;
    bool supports_atom_novelty_query() const override;
    bool test_atom_novelty_read_only(Index atom_index) const override;
    bool supports_transition_novel_witness_query() const override;
    void compute_transition_novel_fluent_atom_indices_read_only(const State& state,
                                                                const State& succ_state,
                                                                AtomIndexList& out_novel_fluent_atom_indices) const override;
    bool supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const override;
    bool supports_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const override;
    bool supports_relaxed_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const override;
    bool test_prune_successor_state_for_beam_selection(const State& state,
                                                       const State& succ_state,
                                                       bool is_new_succ,
                                                       BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_staged_successor_state_for_beam_selection(const State& state,
                                                              const FlatBitset& succ_fluent_atoms,
                                                              const FlatBitset& succ_derived_atoms,
                                                              const FlatDoubleList& succ_numeric_variables,
                                                              const AtomIndexList& succ_fluent_atom_indices,
                                                              bool is_new_succ,
                                                              BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_staged_successor_state_for_relaxed_beam_selection(const State& state,
                                                                      const FlatBitset& succ_fluent_atoms,
                                                                      const FlatBitset& succ_derived_atoms,
                                                                      const FlatDoubleList& succ_numeric_variables,
                                                                      const AtomIndexList& succ_fluent_atom_indices,
                                                                      BeamNoveltyMode beam_novelty_mode) override;
    void on_begin_beam_replay(BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_successor_state_for_beam_replay(const State& state,
                                                    const State& succ_state,
                                                    bool is_new_succ,
                                                    BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_staged_successor_state_for_beam_replay(const State& state,
                                                           const FlatBitset& succ_fluent_atoms,
                                                           const FlatBitset& succ_derived_atoms,
                                                           const FlatDoubleList& succ_numeric_variables,
                                                           const AtomIndexList& succ_fluent_atom_indices,
                                                           bool is_new_succ,
                                                           BeamNoveltyMode beam_novelty_mode) override;
    void on_end_beam_replay(BeamNoveltyMode beam_novelty_mode) override;
};

/// @brief `LandmarkNoveltyPruningStrategyImpl` prunes with landmark-restricted novelty: a state is
/// admitted when it exposes an unseen pair `(l, t)` of a landmark atom `l` true in it and a free
/// atom tuple `t` of size at most `arity`. This is LIW(arity), which sits strictly between
/// IW(arity) and IW(arity+1); see `LandmarkNoveltyTable` for the feature family and its
/// guarantees.
///
/// Which IW(1) accelerators this strategy exposes follows from how much context each of their
/// entry points carries, not from a blanket policy:
///
///   * The add-effect precheck and the transition witness query are supported and **exact**. Both
///     are handed the whole transition -- the precheck as an atom-level delta, the witness query as
///     both states -- which is enough to compute the successor's landmark coordinates and probe the
///     `(coordinate, free tuple)` table directly. The precheck needs the transition's DELETE
///     effects as well as its adds (see `precheck_requires_delete_effects`), because a landmark
///     coordinate can disappear as well as appear.
///
///   * `supports_atom_novelty_query` stays unsupported, and cannot be made exact even in
///     principle: it is handed an atom index alone, with no state and no transition, so there is
///     nothing to quantify the landmark coordinate over. The only sound answer -- "unseen under
///     *some* rank in the whole table" -- is so permissive it buys nothing, and this query is not
///     ordering-only (`IW1ActionPrecheckController::refresh_remaining_atoms` drops atoms from the
///     candidate set on the strength of it), so an over-strict answer would prune wrongly.
///     `iw::find_solution` therefore still rejects `iw1_atom_first_mode` up front.
class LandmarkNoveltyPruningStrategyImpl : public IPruningStrategy
{
private:
    LandmarkNoveltyTable m_novelty_table;

    /// The read-only queries are `const` by interface but need the table's scratch buffers and its
    /// lazy resize, exactly as `DynamicNoveltyTable`'s read-only queries do.
    LandmarkNoveltyTable& mutable_novelty_table() const { return const_cast<LandmarkNoveltyTable&>(m_novelty_table); }

public:
    /// @param grouping which landmark atoms share a novelty row, and which are exempt; see
    /// `LandmarkGrouping`. Default-constructed keeps one row per fact landmark, the behaviour that
    /// predates disjunctive landmarks.
    LandmarkNoveltyPruningStrategyImpl(const landmarks::FactLandmarkGraph& landmarks,
                                       size_t arity,
                                       size_t num_atoms,
                                       LandmarkNoveltyTableOptions table_options = {},
                                       LandmarkGrouping grouping = {});

    static PruningStrategy create(const landmarks::FactLandmarkGraph& landmarks,
                                  size_t arity,
                                  size_t num_atoms,
                                  LandmarkNoveltyTableOptions table_options = {},
                                  LandmarkGrouping grouping = {});

    /// @brief The grouping a graph and `iw::Options` imply: the graph's disjunctive landmarks when
    /// `disjunctive` is set, minus `unshared_atom_indices`, and nothing otherwise.
    static LandmarkGrouping make_grouping(const landmarks::FactLandmarkGraph& landmarks, bool disjunctive, const IndexSet& unshared_atom_indices);

    bool test_prune_initial_state(const State& state) override;
    bool test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ) override;
    bool supports_action_add_effect_precheck() const override;
    bool precheck_requires_delete_effects() const override;
    bool test_transition_novelty_from_add_effects(const State& state,
                                                  const AtomIndexList& add_fluent_atom_indices,
                                                  const AtomIndexList& del_fluent_atom_indices) const override;
    bool supports_transition_novel_witness_query() const override;
    void compute_transition_novel_fluent_atom_indices_read_only(const State& state,
                                                                const State& succ_state,
                                                                AtomIndexList& out_novel_fluent_atom_indices) const override;

    const LandmarkNoveltyTable& get_novelty_table() const;
};

/// @brief Abstracted IW(k), optionally *landmark-restricted*: abstracted LIW(k).
///
/// Passing a landmark graph pairs every abstracted feature tuple with a landmark coordinate,
/// exactly as `LandmarkNoveltyPruningStrategyImpl` pairs a free atom tuple with one. The two
/// widenings are orthogonal and compose: abstraction changes what a *tuple* is (object identities
/// in non-preserved slots become type signatures), while the landmark coordinate changes what a
/// tuple is *indexed by*. Their combination expresses what neither can alone -- a conjunction that
/// is width 2 over identities and still width 2 over abstracted features becomes width 1 once an
/// intermediate landmark ranks it.
///
/// Implemented as one novelty table per landmark rank, so with no graph there is exactly one table
/// and every path below is the pre-landmark one. The split a transition induces is LIW's, and for
/// LIW's reason (see `LandmarkCoordinates::collect_transition`): a rank that flipped on pairs with
/// EVERY tuple of the successor, a rank that was already true only with tuples containing an added
/// atom.
///
/// Beam novelty modes are unsupported while a landmark graph is attached. Their staged entry
/// points are handed a raw successor bitset rather than a `State`, and a landmark coordinate over a
/// staged bitset is a different query than the one `LandmarkCoordinates` answers; claiming support
/// would silently score the beam under the wrong feature family.
class AbstractedNoveltyPruningStrategyImpl : public IPruningStrategy
{
    friend class astar_iw_friend::AbstractedMinimumGNoveltyTable;

private:
    using FeatureId = uint32_t;

    enum class FeatureKind : uint8_t
    {
        ABSTRACTED = 0,
        FULL_ATOM = 1,
    };

    struct FeatureKey
    {
        FeatureKind m_kind;
        Index m_predicate_index;
        Index m_preserved_position;
        Index m_preserved_object_index;
        IndexList m_signature;

        bool operator==(const FeatureKey& other) const noexcept = default;
    };

    struct FeatureKeyHash
    {
        size_t operator()(const FeatureKey& key) const noexcept;
    };

    struct PairKey
    {
        FeatureId m_a;
        FeatureId m_b;

        bool operator==(const PairKey& other) const noexcept = default;
    };

    struct PairKeyHash
    {
        size_t operator()(const PairKey& key) const noexcept;
    };

    struct TripleKey
    {
        FeatureId m_a;
        FeatureId m_b;
        FeatureId m_c;

        bool operator==(const TripleKey& other) const noexcept = default;
    };

    struct TripleKeyHash
    {
        size_t operator()(const TripleKey& key) const noexcept;
    };

    struct AtomIndexListHash
    {
        size_t operator()(const AtomIndexList& atom_indices) const noexcept;
    };

    struct AtomFeatureGroup
    {
        AtomIndex m_atom_index;
        bool m_added;
        const std::vector<FeatureId>* m_features;
    };

    struct GeneratedTuples
    {
        std::vector<FeatureId> m_singles;
        std::vector<PairKey> m_pairs;
        std::vector<TripleKey> m_triples;
    };

    class NoveltyTables
    {
    private:
        size_t m_width;
        bool m_dense_singletons;
        absl::flat_hash_set<FeatureId> m_seen_singletons;
        absl::flat_hash_set<FeatureId> m_delta_singletons;
        std::vector<uint8_t> m_seen_singletons_dense;
        std::vector<uint8_t> m_delta_singletons_dense;
        std::vector<FeatureId> m_delta_singleton_touched;
        absl::flat_hash_set<PairKey, PairKeyHash> m_seen_pairs;
        absl::flat_hash_set<PairKey, PairKeyHash> m_delta_pairs;
        absl::flat_hash_set<TripleKey, TripleKeyHash> m_seen_triples;
        absl::flat_hash_set<TripleKey, TripleKeyHash> m_delta_triples;

        void ensure_dense_singleton_capacity(FeatureId feature);

    public:
        explicit NoveltyTables(size_t width);
        bool contains_single(FeatureId feature) const;
        bool contains_pair(PairKey pair) const;
        bool contains_triple(TripleKey triple) const;
        bool contains_single_or_delta(FeatureId feature) const;
        bool contains_pair_or_delta(PairKey pair) const;
        bool contains_triple_or_delta(TripleKey triple) const;
        bool insert_single(FeatureId feature);
        bool insert_pair(PairKey pair);
        bool insert_triple(TripleKey triple);
        bool insert_delta_single(FeatureId feature);
        bool insert_delta_pair(PairKey pair);
        bool insert_delta_triple(TripleKey triple);
        void clear_delta();
        void reserve_singletons(size_t count);
        void commit_delta();
    };

    formalism::Problem m_problem;
    size_t m_width;
    bool m_base_abstracted;
    bool m_preserve_goal_atoms;
    bool m_keep_depth_one_novel;
    std::optional<Index> m_root_state_index;
    mutable absl::flat_hash_map<FeatureKey, FeatureId, FeatureKeyHash> m_feature_ids;
    mutable std::vector<std::vector<FeatureId>> m_features_by_atom_index;
    absl::flat_hash_set<AtomIndex> m_goal_fluent_atom_indices;
    absl::flat_hash_set<Index> m_skip_depth_one_expansion_state_indices;
    absl::flat_hash_set<AtomIndexList, AtomIndexListHash> m_skip_depth_one_expansion_fluent_atom_indices_fallback;

    /* One `NoveltyTables` per landmark rank. Without a landmark graph the vector holds exactly one
       entry, `m_active_tables` never moves off it, and every query below is the pre-landmark one --
       which is what keeps abstracted IW(k) byte-identical. A `NoveltyTables` starts empty and grows
       lazily, so ranks the search never reaches cost a header each. */
    mutable std::vector<NoveltyTables> m_tables_by_rank;
    mutable NoveltyTables* m_active_tables;
    std::optional<LandmarkCoordinates> m_landmark_coordinates;

    /* Rank scratch, reused across queries for the same reason the tuple scratch is. */
    mutable std::vector<uint32_t> m_scratch_ranks;
    mutable std::vector<uint32_t> m_scratch_flipped_ranks;
    mutable std::vector<uint32_t> m_scratch_kept_ranks;
    mutable AtomIndexList m_scratch_true_landmark_atoms;
    mutable std::vector<uint32_t> m_scratch_rank_carrier_counts;
    /* `L(state)` is per state while the precheck is per action, so the two above are cached for
       the state the caller is currently looping the actions of. */
    mutable std::optional<Index> m_delta_query_state_index;
    /* Whether a rank's tables have had their dense singleton row reserved. Deferred to first use:
       reserving eagerly would cost `num_ranks x num_features` bytes for ranks the search never
       reaches, and most ranks of a large landmark graph are never reached. */
    mutable std::vector<uint8_t> m_rank_reserved;

    /* Tuple-generation scratch, reused across novelty tests instead of reallocated per call.
       Mutable for the same reason `LandmarkNoveltyTable`'s scratch is: one strategy instance
       serves one search on one thread. */
    mutable std::vector<AtomFeatureGroup> m_scratch_atom_feature_groups;
    mutable GeneratedTuples m_scratch_generated_tuples;
    mutable AtomIndexList m_scratch_atom_indices_key;
    mutable absl::flat_hash_set<FeatureId> m_scratch_local_singletons;
    mutable absl::flat_hash_set<PairKey, PairKeyHash> m_scratch_local_pairs;
    mutable absl::flat_hash_set<TripleKey, TripleKeyHash> m_scratch_local_triples;
    mutable std::vector<size_t> m_scratch_added_group_indices;

    /// @brief Whether an attached landmark graph makes this abstracted LIW(k) rather than IW(k).
    bool has_landmark_coordinate() const { return m_landmark_coordinates.has_value(); }
    /// @brief The tables of the rank currently being queried.
    NoveltyTables& tables() const { return *m_active_tables; }
    /// @brief Point `tables()` at `rank`.
    void activate_rank(uint32_t rank) const;
    /// @brief Run `query` once per rank in `ranks`, ORing the results WITHOUT short-circuiting.
    ///
    /// Not short-circuiting is the point for the updating variants: every rank the transition
    /// exposes has to be recorded, or a later state re-derives it as novel.
    template<typename Query>
    bool for_each_rank(const std::vector<uint32_t>& ranks, Query&& query) const
    {
        auto any = false;
        for (const auto rank : ranks)
        {
            activate_rank(rank);
            any = query() || any;
        }
        return any;
    }
    /// @brief `collect_transition` for this strategy: the ranks of a transition, split.
    void collect_transition_ranks(const State& state, const State& succ_state) const;
    /// @brief Refresh the per-state landmark caches the delta precheck reads.
    void refresh_delta_query_state(const State& state) const;
    /// @brief `test_transition_novelty_and_update_table` under a landmark coordinate.
    bool test_landmark_transition_novelty_and_update_table(const State& state, const State& succ_state);
    /// @brief The width-1 lane of a landmark-restricted transition.
    ///
    /// One pass over the successor's atoms with the ranks on the inside, rather than one pass per
    /// rank: an atom's abstracted features are fetched once and probed against every rank that
    /// wants them. Which ranks want them is the LIW split -- a flipped rank takes every atom, a
    /// kept rank only the added ones.
    bool test_landmark_transition_width_one(const State& state, const State& succ_state, bool update);

    void precompute_goal_atom_indices();
    void precompute_atom_features();
    void ensure_atom_feature_capacity(AtomIndex atom_index) const;
    const std::vector<FeatureId>& get_atom_features(AtomIndex atom_index) const;
    FeatureId intern_feature(const FeatureKey& key) const;
    std::vector<FeatureId> compute_features_for_atom(formalism::GroundAtom<formalism::FluentTag> atom) const;
    FeatureKey make_full_atom_key(formalism::GroundAtom<formalism::FluentTag> atom) const;
    FeatureKey make_abstracted_key(formalism::GroundAtom<formalism::FluentTag> atom, Index preserved_position) const;
    void append_object_type_signature(formalism::Object object, IndexList& out) const;
    void state_groups(const State& state, std::vector<AtomFeatureGroup>& out_groups) const;
    std::vector<AtomFeatureGroup> state_groups(const State& state) const;
    void successor_groups(const State& state, const AtomIndexList& succ_fluent_atom_indices, std::vector<AtomFeatureGroup>& out_groups) const;
    std::vector<AtomFeatureGroup> successor_groups(const State& state, const AtomIndexList& succ_fluent_atom_indices) const;
    void atom_indices_key(const State& state, AtomIndexList& out_atom_indices) const;
    AtomIndexList atom_indices_key(const State& state) const;
    bool test_atom_novelty(AtomIndex atom_index) const;
    bool test_atom_novelty_and_update_table(AtomIndex atom_index);
    bool test_atom_novelty_and_update_delta(AtomIndex atom_index);
    bool test_state_novelty_and_update_table(const State& state);
    bool test_transition_novelty(const State& state, const State& succ_state) const;
    bool test_transition_novelty(const State& state, const AtomIndexList& succ_fluent_atom_indices) const;
    bool test_transition_novelty_and_update_table(const State& state, const State& succ_state);
    bool test_transition_novelty_and_update_table(const State& state, const AtomIndexList& succ_fluent_atom_indices);
    bool test_transition_novelty_and_update_delta(const State& state, const State& succ_state);
    bool test_transition_novelty_and_update_delta(const State& state, const AtomIndexList& succ_fluent_atom_indices);
    bool test_transition_and_update(const State& state, const AtomIndexList& succ_fluent_atom_indices, bool use_delta);
    void generate_tuples(const std::vector<AtomFeatureGroup>& groups, bool use_delta, GeneratedTuples& out_tuples) const;
    GeneratedTuples generate_tuples(const std::vector<AtomFeatureGroup>& groups, bool use_delta) const;
    void insert_tuples(const GeneratedTuples& tuples);
    void insert_delta_tuples(const GeneratedTuples& tuples);
    bool is_single_novel(FeatureId feature, bool use_delta) const;
    bool is_pair_novel(PairKey pair, bool use_delta) const;
    bool is_triple_novel(TripleKey triple, bool use_delta) const;
    bool maybe_prune_depth_one_successor(const State& state, const State& succ_state, bool is_novel);
    bool maybe_prune_staged_depth_one_successor(const State& state, const AtomIndexList& succ_fluent_atom_indices, bool is_novel);

public:
    /// Abstracted IW(k) replaces object identities in non-preserved argument slots by
    /// either their type signature (AIW) or a universal type (BAIW). Positive goal atoms
    /// can be kept as full identity atoms to match the paper-faithful goal-detection rule.
    /// @param landmarks attach to run abstracted LIW(k) instead of abstracted IW(k); null (the
    /// default) keeps one novelty table and the pre-landmark behaviour exactly.
    /// @param grouping which landmark atoms share a novelty row; see `LandmarkGrouping`. Ignored
    /// without `landmarks`.
    explicit AbstractedNoveltyPruningStrategyImpl(formalism::Problem problem,
                                                  size_t width = 1,
                                                  bool base_abstracted = false,
                                                  bool preserve_goal_atoms = true,
                                                  bool keep_depth_one_novel = false,
                                                  landmarks::FactLandmarkGraph landmarks = nullptr,
                                                  LandmarkGrouping grouping = {});

    static PruningStrategy create(formalism::Problem problem,
                                  size_t width = 1,
                                  bool base_abstracted = false,
                                  bool preserve_goal_atoms = true,
                                  bool keep_depth_one_novel = false,
                                  landmarks::FactLandmarkGraph landmarks = nullptr,
                                  LandmarkGrouping grouping = {});

    /// @brief Whether this instance pairs abstracted tuples with a landmark coordinate.
    bool is_landmark_restricted() const { return m_landmark_coordinates.has_value(); }
    /// @brief Number of landmark ranks, i.e. 1 without a landmark graph.
    size_t get_num_landmark_ranks() const { return m_tables_by_rank.size(); }

    bool test_prune_initial_state(const State& state) override;
    bool test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ) override;
    bool supports_action_add_effect_precheck() const override;
    bool should_bypass_action_add_effect_precheck(const State& state) const override;
    /// @brief True under a landmark coordinate: a rank can disappear as well as appear, so the
    /// precheck cannot decide the successor's coordinates from the adds alone.
    bool precheck_requires_delete_effects() const override;
    bool test_transition_novelty_from_add_effects(const State& state,
                                                  const AtomIndexList& add_fluent_atom_indices,
                                                  const AtomIndexList& del_fluent_atom_indices) const override;
    bool consume_skip_state_expansion(const State& state) override;
    bool supports_atom_novelty_query() const override;
    bool test_atom_novelty_read_only(Index atom_index) const override;
    bool supports_transition_novel_witness_query() const override;
    void compute_transition_novel_fluent_atom_indices_read_only(const State& state,
                                                                const State& succ_state,
                                                                AtomIndexList& out_novel_fluent_atom_indices) const override;
    bool supports_beam_novelty_mode(BeamNoveltyMode beam_novelty_mode) const override;
    bool supports_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const override;
    bool supports_relaxed_staged_beam_pruning(BeamNoveltyMode beam_novelty_mode) const override;
    bool test_prune_successor_state_for_beam_selection(const State& state,
                                                       const State& succ_state,
                                                       bool is_new_succ,
                                                       BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_staged_successor_state_for_beam_selection(const State& state,
                                                              const FlatBitset& succ_fluent_atoms,
                                                              const FlatBitset& succ_derived_atoms,
                                                              const FlatDoubleList& succ_numeric_variables,
                                                              const AtomIndexList& succ_fluent_atom_indices,
                                                              bool is_new_succ,
                                                              BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_staged_successor_state_for_relaxed_beam_selection(const State& state,
                                                                      const FlatBitset& succ_fluent_atoms,
                                                                      const FlatBitset& succ_derived_atoms,
                                                                      const FlatDoubleList& succ_numeric_variables,
                                                                      const AtomIndexList& succ_fluent_atom_indices,
                                                                      BeamNoveltyMode beam_novelty_mode) override;
    void on_begin_beam_replay(BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_successor_state_for_beam_replay(const State& state,
                                                    const State& succ_state,
                                                    bool is_new_succ,
                                                    BeamNoveltyMode beam_novelty_mode) override;
    bool test_prune_staged_successor_state_for_beam_replay(const State& state,
                                                           const FlatBitset& succ_fluent_atoms,
                                                           const FlatBitset& succ_derived_atoms,
                                                           const FlatDoubleList& succ_numeric_variables,
                                                           const AtomIndexList& succ_fluent_atom_indices,
                                                           bool is_new_succ,
                                                           BeamNoveltyMode beam_novelty_mode) override;
    void on_end_beam_replay(BeamNoveltyMode beam_novelty_mode) override;
};

}

#endif
