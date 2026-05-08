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

#include "mimir/search/algorithms/iw/novelty_table.hpp"
#include "mimir/search/algorithms/iw/tuple_index_mapper.hpp"
#include "mimir/search/algorithms/strategies/pruning_strategy.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/search/state.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_set>

namespace mimir::search::iw
{
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
    bool test_transition_novelty_from_add_effects(const State& state, const AtomIndexList& add_fluent_atom_indices) const override;
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


class AbstractedNoveltyPruningStrategyImpl : public IPruningStrategy
{
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
    mutable NoveltyTables m_tables;

    void precompute_goal_atom_indices();
    void precompute_atom_features();
    void ensure_atom_feature_capacity(AtomIndex atom_index) const;
    const std::vector<FeatureId>& get_atom_features(AtomIndex atom_index) const;
    FeatureId intern_feature(const FeatureKey& key) const;
    std::vector<FeatureId> compute_features_for_atom(formalism::GroundAtom<formalism::FluentTag> atom) const;
    FeatureKey make_full_atom_key(formalism::GroundAtom<formalism::FluentTag> atom) const;
    FeatureKey make_abstracted_key(formalism::GroundAtom<formalism::FluentTag> atom, Index preserved_position) const;
    void append_object_type_signature(formalism::Object object, IndexList& out) const;
    std::vector<AtomFeatureGroup> state_groups(const State& state) const;
    std::vector<AtomFeatureGroup> successor_groups(const State& state, const AtomIndexList& succ_fluent_atom_indices) const;
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
    explicit AbstractedNoveltyPruningStrategyImpl(formalism::Problem problem,
                                                  size_t width = 1,
                                                  bool base_abstracted = false,
                                                  bool preserve_goal_atoms = true,
                                                  bool keep_depth_one_novel = false);

    static PruningStrategy create(formalism::Problem problem,
                                  size_t width = 1,
                                  bool base_abstracted = false,
                                  bool preserve_goal_atoms = true,
                                  bool keep_depth_one_novel = false);

    bool test_prune_initial_state(const State& state) override;
    bool test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ) override;
    bool supports_action_add_effect_precheck() const override;
    bool should_bypass_action_add_effect_precheck(const State& state) const override;
    bool test_transition_novelty_from_add_effects(const State& state, const AtomIndexList& add_fluent_atom_indices) const override;
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
