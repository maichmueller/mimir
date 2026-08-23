#ifndef MIMIR_SEARCH_ALGORITHMS_ASTAR_IW_HPP_
#define MIMIR_SEARCH_ALGORITHMS_ASTAR_IW_HPP_

#include "mimir/common/types_cista.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/search/algorithms/utils.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/search/state.hpp"

#include <limits>
#include <optional>

namespace mimir::search::astar_iw
{

enum class NoveltyFeatureMode
{
    CLASSICAL,
    ABSTRACTED,
    BASE_ABSTRACTED,
};

struct Options
{
    std::optional<State> start_state = std::nullopt;
    EventHandler event_handler = nullptr;
    GoalStrategy goal_strategy = nullptr;
    size_t width = 1;
    NoveltyFeatureMode novelty_feature_mode = NoveltyFeatureMode::CLASSICAL;

    /// @brief When set, novelty is restricted to `(landmark atom true in the state, free tuple of
    /// size at most width)` pairs -- the LIW(width) feature family, which prunes less than
    /// IW(width) and more than IW(width+1) while staying linear in the number of landmarks. See
    /// `iw::LandmarkMinimumGNoveltyTable`.
    ///
    /// Composes with every `novelty_feature_mode`: the landmark coordinate is always a concrete
    /// landmark rank, and only the free coordinates are abstracted in the abstracted modes.
    landmarks::FactLandmarkGraph landmark_novelty_graph = nullptr;
    bool preserve_goal_atoms = true;
    ContinuousCost heuristic_weight = 1.0;
    bool allow_non_novel_root_goal = true;

    /// @brief Reject a successor on novelty before evaluating the heuristic on it.
    ///
    /// Most generated successors fail the novelty test, and a heuristic evaluation costs far more
    /// than one, so testing first skips the majority of that work -- worth roughly half the search
    /// time under `h_FF`. The test is done as a read-only probe followed by the usual update, so
    /// neither a rejected successor nor a dead end writes to the novelty table and the search is
    /// unchanged; only a successor that is both non-novel and a dead end moves between the
    /// `num_deadends` and `num_novelty_rejected` counters.
    ///
    /// Turn it off for a blind search: the probe is then pure overhead, since the evaluation it
    /// avoids is free.
    bool probe_novelty_before_heuristic = true;
    uint32_t max_num_states = std::numeric_limits<uint32_t>::max();
    uint32_t max_time_in_ms = std::numeric_limits<uint32_t>::max();
};

/// @brief Run weighted A* with minimum-g width pruning.
///
/// Queue priority is `(g + heuristic_weight * h, h, g, state index)`. The
/// implementation requires unit-cost transitions so tuple minimum-g labels
/// coincide with IW search depths. Ordinary A* optimality is not implied by
/// novelty pruning, and weights greater than one are satisficing guidance.
extern SearchResult find_solution(const SearchContext& context, const Heuristic& heuristic, const Options& options = Options());

}

#endif
