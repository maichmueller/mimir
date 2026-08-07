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

#ifndef MIMIR_SEARCH_ALGORITHMS_BRFS_HPP_
#define MIMIR_SEARCH_ALGORITHMS_BRFS_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/search/algorithms/search_control.hpp"
#include "mimir/search/algorithms/utils.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/search/state.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace mimir::search::brfs
{
struct Options
{
    std::optional<State> start_state = std::nullopt;
    EventHandler event_handler = nullptr;
    GoalStrategy goal_strategy = nullptr;
    PruningStrategy pruning_strategy = nullptr;
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
    bool stop_if_goal = true;
    uint32_t max_depth = std::numeric_limits<uint32_t>::max();
    uint32_t max_num_states = std::numeric_limits<uint32_t>::max();
    uint32_t max_time_in_ms = std::numeric_limits<uint32_t>::max();

    /// @brief Optional coordination with searches running alongside this one. Null means "run
    /// alone", and costs one predictable branch per node pop.
    ///
    /// When set, this search stops promptly on `cancel`, counts its expansions into
    /// `total_expansions`, and -- because the goal is tested when a node is *popped* -- publishes
    /// each fully expanded g-layer into `completed_depth`. Finishing layer d proves no plan of
    /// length <= d exists in the space this search explores, which is what turns a plan somebody
    /// else found into a *certified* shortest one. It also stops itself once its own progress has
    /// certified the current incumbent, since there is nothing left to prove.
    ///
    /// Only honored on the plain queued path. The beam, ordered-layer and deferred-novelty paths
    /// reject it rather than ignoring it: their layer bookkeeping does not carry the meaning the
    /// certificate needs.
    SearchControl* control = nullptr;

    Options() = default;
};

extern SearchResult find_solution(const SearchContext& context, const Options& options = Options());

/// @brief Overload that reorders each search layer's candidate transitions by landmark score before
/// novelty pruning decides admission (see `LandmarkTransitionOrderingStrategy`). Only a small subset of
/// `Options` is supported alongside a landmark ordering; see brfs.cpp for the rejected combinations.
extern SearchResult find_solution(const SearchContext& context, const Options& options, const LandmarkTransitionOrderingStrategy& ordering);

}

#endif
