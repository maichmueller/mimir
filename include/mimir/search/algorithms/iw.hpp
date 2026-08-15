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

#ifndef MIMIR_SEARCH_ALGORITHMS_IW_HPP_
#define MIMIR_SEARCH_ALGORITHMS_IW_HPP_

#include "mimir/search/algorithms/brfs.hpp"
#include "mimir/search/algorithms/iw/landmark_novelty_table.hpp"
#include "mimir/search/state.hpp"

namespace mimir::search::iw
{
struct Options
{
    /// @brief Where to start, defaulting to the search context's initial state.
    ///
    /// ATTENTION: the state is looked up in the search context's OWN state repository. A `State`
    /// obtained from a different context -- including one over the same `Problem` -- is not a valid
    /// key there, and the failure surfaces as an `IndexError` thrown from inside the search rather
    /// than at the call site. Re-create the start state through this context's repository (see
    /// `StateRepositoryImpl::get_or_create_state`) before handing it over.
    std::optional<State> start_state = std::nullopt;
    EventHandler iw_event_handler = nullptr;
    brfs::EventHandler brfs_event_handler = nullptr;
    GoalStrategy goal_strategy = nullptr;
    LayerOrderingStrategy layer_ordering_strategy = nullptr;
    uint32_t max_next_layer_states = std::numeric_limits<uint32_t>::max();
    uint32_t beam_width = std::numeric_limits<uint32_t>::max();
    BeamNoveltyMode beam_novelty_mode = BeamNoveltyMode::ALL_TESTED;
    bool relaxed_survivors_only_beam = false;
    bool randomize_equal_score_ties = false;
    uint64_t equal_score_tie_seed = 0;
    uint32_t parallel_beam_num_threads = 1;
    uint32_t parallel_beam_chunk_size = 1024;
    bool iw1_precheck_add_effect_novelty = false;
    bool iw1_atom_first_mode = false;
    double iw1_atom_first_ratio = 1.0;
    bool iw1_incremental_first_applicability = false;
    bool iw1_incremental_first_applicability_debug_crosscheck = false;
    uint32_t max_depth = std::numeric_limits<uint32_t>::max();
    size_t max_arity = MAX_ARITY - 1;

    /// @brief When set, the search becomes LIW rather than IW: it escalates through
    /// `0, LIW(1), LIW(2), ...` up to `max_arity`, where LIW(k) tracks pairs of a landmark atom
    /// true in the state and a free atom tuple of size at most `k`.
    ///
    /// LIW(k) prunes less than IW(k) and more than IW(k+1) while its table stays linear in the
    /// number of landmarks, so `max_arity` still counts the *free* coordinates -- LIW(1) tuples
    /// have size two. This is a variant ladder in its own right, not a refinement inserted into
    /// IW's: an arity-k rung is either IW(k) or LIW(k), never both, so `max_arity` keeps its
    /// meaning and one pass still corresponds to one width. See `LandmarkNoveltyTable` for the
    /// feature family and its guarantees, and `LandmarkNoveltyPruningStrategyImpl` for the
    /// pruning rule. The arity-0 pass is unaffected.
    ///
    /// An empty landmark graph degrades this to exactly plain IW, expansion for expansion.
    ///
    /// This is incompatible with the `iw1_*` accelerators, which all reason about atom-level
    /// novelty; combining them is rejected rather than silently ignored.
    landmarks::FactLandmarkGraph landmark_novelty_graph = nullptr;

    /// @brief Storage budget for the landmark novelty tables; ignored without
    /// `landmark_novelty_graph`. See `iw::LandmarkNoveltyTableOptions`.
    LandmarkNoveltyTableOptions landmark_novelty_table_options = {};

    /// @brief Wall-clock budget for the whole search, spanning every arity pass. Each pass is given
    /// what is left of it, so raising `max_arity` cannot silently multiply the time spent.
    uint32_t max_time_in_ms = std::numeric_limits<uint32_t>::max();

    /// @brief Cap on the search nodes of a *single* arity pass, mirroring `max_depth`. Passes do not
    /// share a node budget: each one restarts from the start state with its own search tree.
    uint32_t max_num_states = std::numeric_limits<uint32_t>::max();

    /// @brief Optional coordination with searches running alongside this one; see
    /// `brfs::Options::control`. Forwarded to every arity pass, so an IW(1) run used as a
    /// portfolio's certifier publishes its completed depths through it.
    SearchControl* control = nullptr;

    Options() = default;
};

extern SearchResult find_solution(const SearchContext& context, const Options& options = Options());

/// @brief Overload that applies a deferred-novelty transition ordering strategy (see
/// `LandmarkTransitionOrderingStrategy`) to the width-1 BrFS pass only; arity 0 and arity > 1 passes
/// stay on the default queued path (see iw.cpp).
extern SearchResult find_solution(const SearchContext& context, const Options& options, const LandmarkTransitionOrderingStrategy& ordering);
}

#endif
