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

#ifndef MIMIR_SEARCH_ALGORITHMS_ROLLOUT_IW_HPP_
#define MIMIR_SEARCH_ALGORITHMS_ROLLOUT_IW_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/search/algorithms/rollout_iw/action_ordering.hpp"
#include "mimir/search/algorithms/search_control.hpp"
#include "mimir/search/algorithms/utils.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/search/state.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

/// @brief Rollout IW(1) after Bandres, Bonet and Geffner, *Planning With Pixels in (Almost) Real
/// Time* (AAAI 2018), https://arxiv.org/abs/1801.03354.
///
/// This is a different algorithm from `iw::find_rollouts_parallel`, which runs K ordinary IW/BrFS
/// searches with randomized layer orderings. Rollout IW descends a single path across depth layers,
/// keeps the minimum depth at which each feature has been seen, and labels subtrees SOLVED as they
/// are proven unable to contribute anything novel. Neither API is a renaming of the other.
///
/// Features are the ground fluent atoms, matching IW(1) elsewhere in Mimir. The feature-depth table
/// grows on demand, so a lifted context whose ground atom universe is discovered during search
/// needs no pre-grounding.
namespace mimir::search::rollout_iw
{

/// @brief One plan step as a lifted schema plus its binding.
///
/// The portable form of a plan. A `GroundAction` belongs to the repository that interned it, so it
/// cannot leave a grounding overlay; a schema and a list of objects are stable in the parent problem
/// and can be re-grounded anywhere. See `ProblemImpl::create_grounding_overlay`.
struct PlanStep
{
    formalism::Action schema = nullptr;
    formalism::ObjectList binding;
};

using PlanStepList = std::vector<PlanStep>;

struct Options
{
    std::optional<State> start_state = std::nullopt;

    /// @brief The goal to search for. Used to build the default goal strategy, and to aim the
    /// goal-directed action orderings. Defaults to the problem's own goal condition.
    std::optional<formalism::GroundConjunctiveCondition> goal_condition = std::nullopt;

    /// @brief Native goal strategy. Built per search, never shared across threads: `IGoalStrategy`
    /// methods are non-const. Defaults to one testing `goal_condition`.
    GoalStrategy goal_strategy = nullptr;

    /// @brief Guidance for which applicable action to try first. Defaults to generator order.
    ActionOrderingStrategy action_ordering = nullptr;

    /// @brief Which built-in ordering to construct when `action_ordering` is null. Saves the caller
    /// from having to know that the goal-directed ones need the problem and the goal to be built.
    std::optional<ActionOrderingConfiguration> action_ordering_configuration = std::nullopt;

    uint64_t seed = 0;
    uint32_t max_depth = std::numeric_limits<uint32_t>::max();
    uint64_t max_rollouts = std::numeric_limits<uint64_t>::max();
    uint64_t max_num_states = std::numeric_limits<uint64_t>::max();
    uint32_t max_time_in_ms = std::numeric_limits<uint32_t>::max();

    /// @brief Optional shared stop flag and incumbent length. Null means "run alone"; the checks
    /// against it cost one predictable branch per step.
    SearchControl* control = nullptr;

    /// @brief Length of a plan already known elsewhere. A non-goal node at depth `d` can only lead
    /// to plans of length `d + 1` or more, so once `d + 1 >= incumbent_bound` that branch cannot
    /// improve on what is known and is labeled SOLVED. The goal is always tested first, so a goal
    /// exactly at the boundary is still found. Combined with `control->incumbent_length` when both
    /// are present, taking whichever is tighter at the time of the check.
    uint32_t incumbent_bound = std::numeric_limits<uint32_t>::max();

    Options() = default;
};

struct Statistics
{
    uint64_t num_rollouts = 0;
    uint64_t num_generated_states = 0;
    uint64_t num_expanded_nodes = 0;  ///< nodes whose applicable actions were materialized
    uint64_t num_feature_depth_improvements = 0;

    /// New node that lowered some feature's minimum depth; the rollout continues through it.
    uint64_t num_case_1 = 0;
    /// New node that lowered nothing; it is labeled SOLVED and the rollout ends.
    uint64_t num_case_2 = 0;
    /// Existing node no longer at the best known depth of any of its features; SOLVED, rollout ends.
    uint64_t num_case_3 = 0;
    /// Existing node still at the best known depth of some feature; the rollout continues through it.
    uint64_t num_case_4 = 0;

    uint64_t num_solved_propagations = 0;
    uint64_t num_dead_ends = 0;                  ///< nodes with no applicable action at all
    uint64_t num_depth_bound_prunings = 0;       ///< subtrees cut by `max_depth`
    uint64_t num_incumbent_bound_prunings = 0;   ///< subtrees cut because they cannot beat the incumbent
    uint32_t max_rollout_depth = 0;
    uint64_t num_tree_nodes = 0;
};

struct Result
{
    SearchResult search_result;
    Statistics statistics;

    /// @brief Whether the root was labeled SOLVED, i.e. the reachable width-1 space was exhausted.
    /// Read it together with the bound-pruning counters: if either is non-zero, the exhaustion is
    /// relative to `max_depth`/`incumbent_bound` rather than absolute.
    bool root_solved = false;

    /// @brief The found plan as schema/binding steps, empty unless the status is SOLVED.
    PlanStepList plan_steps;

    uint32_t plan_length = std::numeric_limits<uint32_t>::max();

    /// @brief Human-readable reason the search stopped, for results that are not SOLVED.
    std::string stop_reason;
};

/// @brief Run Rollout IW(1) on `context`.
///
/// Works on grounded and on lifted KPKC contexts alike, and assumes nothing about the problem's
/// repositories being frozen: it is the sole user of the context it is handed, so grounding into it
/// is safe. Running several of these concurrently is the portfolio's job, and requires each to have
/// its own context (see `iw::find_solution_atomic_goal_portfolio`).
extern Result find_solution(const SearchContext& context, const Options& options = Options());

}

#endif
