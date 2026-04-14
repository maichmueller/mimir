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
    std::vector<AtomIndexList> m_beam_layer_delta_tuples;
    std::unordered_set<AtomIndexList, AtomIndexListHash> m_beam_layer_delta_tuple_set;
    std::vector<AtomIndexList> m_scratch_novel_tuples;

    bool test_transition_novelty(const State& state, const State& succ_state);
    bool test_transition_novelty_and_update_delta(const State& state, const State& succ_state);

public:
    ArityKNoveltyPruningStrategyImpl(size_t arity, size_t num_atoms);

    static PruningStrategy create(size_t arity, size_t num_atoms);

    bool test_prune_initial_state(const State& state) override;
    bool test_prune_successor_state(const State& state, const State& succ_state, bool is_new_succ) override;
    bool supports_action_add_effect_precheck() const override;
    bool test_transition_novelty_from_add_effects(const State& state, const AtomIndexList& add_fluent_atom_indices) const override;
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

class ProjectiveArityOneNoveltyPruningStrategyImpl : public IPruningStrategy
{
private:
    enum class ProjectionKind : Index
    {
        UNARY = 0,
        UNTYPED = 1,
        TYPED = 2
    };

    struct ProjectedAtomKey
    {
        ProjectionKind m_kind;
        Index m_predicate_index;
        Index m_position;
        Index m_projected_object_index;
        IndexList m_other_slot_type_signature;

        bool operator==(const ProjectedAtomKey& other) const noexcept = default;
    };

    struct ProjectedAtomKeyHash
    {
        size_t operator()(const ProjectedAtomKey& key) const noexcept;
    };

    struct AtomIndexListHash
    {
        size_t operator()(const AtomIndexList& atom_indices) const noexcept;
    };

    formalism::Problem m_problem;
    bool m_typed_projection;
    bool m_keep_depth_one_novel;
    bool m_keep_goal_nonunary_atoms;
    std::optional<Index> m_root_state_index;
    mutable std::vector<std::vector<ProjectedAtomKey>> m_projected_atom_keys_by_atom_index;
    std::unordered_set<ProjectedAtomKey, ProjectedAtomKeyHash> m_seen_projected_atoms;
    std::vector<ProjectedAtomKey> m_beam_layer_delta_projected_atoms;
    std::unordered_set<ProjectedAtomKey, ProjectedAtomKeyHash> m_beam_layer_delta_projected_atoms_set;
    std::unordered_set<Index> m_skip_depth_one_expansion_state_indices;
    std::unordered_set<AtomIndexList, AtomIndexListHash> m_skip_depth_one_expansion_fluent_atom_indices_fallback;

    void compute_projected_atom_keys_for_atom(formalism::GroundAtom<formalism::FluentTag> ground_atom,
                                              std::vector<ProjectedAtomKey>& out_projected_atom_keys) const;
    void precompute_projected_atom_keys();
    const std::vector<ProjectedAtomKey>& get_projected_atom_keys(AtomIndex atom_index) const;
    bool test_atom_novelty(AtomIndex atom_index) const;
    bool test_atom_novelty_and_update_table(AtomIndex atom_index);
    bool test_atom_novelty_and_update_delta(AtomIndex atom_index);
    bool test_state_novelty_and_update_table(const State& state);
    bool test_transition_novelty(const State& state, const State& succ_state) const;
    bool test_transition_novelty_and_update_table(const State& state, const State& succ_state);
    bool test_transition_novelty_and_update_delta(const State& state, const State& succ_state);

public:
    /// Projective IW(1) keeps the usual width-1 novelty test, but it augments the atom set:
    /// every non-unary atom p(x1, ..., xn) is split into positional unary projections p[i](xi).
    /// Novelty is then checked on these projected features instead of only on the original atom.
    /// Optionally, positive goal atoms of arity > 1 can also stay as full atoms in this feature set.
    explicit ProjectiveArityOneNoveltyPruningStrategyImpl(formalism::Problem problem,
                                                          bool typed_projection = false,
                                                          bool keep_depth_one_novel = false,
                                                          bool keep_goal_nonunary_atoms = false);

    static PruningStrategy create(formalism::Problem problem,
                                  bool typed_projection = false,
                                  bool keep_depth_one_novel = false,
                                  bool keep_goal_nonunary_atoms = false);

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
