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
#include "mimir/search/state.hpp"

namespace mimir::search::iw
{
struct Options
{
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

    Options() = default;
};

extern SearchResult find_solution(const SearchContext& context, const Options& options = Options());

/// @brief Overload that applies a deferred-novelty transition ordering strategy (see
/// `LandmarkTransitionOrderingStrategy`) to the width-1 BrFS pass only; arity 0 and arity > 1 passes
/// stay on the default queued path (see iw.cpp).
extern SearchResult find_solution(const SearchContext& context, const Options& options, const LandmarkTransitionOrderingStrategy& ordering);
}

#endif
