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

    Options() = default;
};

extern SearchResult find_solution(const SearchContext& context, const Options& options = Options());

}

#endif
