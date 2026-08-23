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

#ifndef MIMIR_SEARCH_ALGORITHMS_IW_ATOMIC_GOAL_PORTFOLIO_HPP_
#define MIMIR_SEARCH_ALGORITHMS_IW_ATOMIC_GOAL_PORTFOLIO_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/search/algorithms/brfs/event_handlers/statistics.hpp"
#include "mimir/search/algorithms/rollout_iw.hpp"
#include "mimir/search/algorithms/utils.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/search/state.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace mimir::search
{

enum class AtomicGoalPortfolioSearchMode
{
    /// @brief One shared grounded generator, a private state repository per worker. The proven fast
    /// path from `docs/PARALLEL_IW_ROLLOUTS.md`, and the right choice whenever the instance can be
    /// pre-grounded at all.
    GROUNDED,
    /// @brief One grounding overlay per worker over a shared lifted model, each with its own KPKC
    /// generator and axiom evaluator. Never pre-grounds anything.
    LIFTED_KPKC,
    /// @brief Pick from what the given context already is.
    INHERIT_CONTEXT,
};

}

namespace mimir::search::iw
{

/// @brief One canonical IW(1) certifier plus K goal-guided Rollout IW(1) accelerators, over one
/// atomic goal, run in parallel.
///
/// The two roles are different on purpose. The rollout workers find *a* plan quickly and keep
/// improving on it; the certifier proves when a plan is as short as it gets. Because breadth-first
/// search tests the goal when a node is popped, finishing depth `d` rules out every plan of length
/// `d` or less, so an incumbent of length `L` is certified the moment the certifier completes depth
/// `L - 1` -- which is usually long before it would have found the plan itself.
struct AtomicGoalPortfolioOptions
{
    std::optional<State> start_state = std::nullopt;

    /// @brief The goal to search for. Canonicalized in the parent problem before any worker starts,
    /// so all workers test the identical condition. Defaults to the problem's own goal.
    std::optional<formalism::GroundConjunctiveCondition> atomic_goal = std::nullopt;

    /// @brief Convenience alternative to `atomic_goal`: positive fluent atoms to be interned into a
    /// goal condition on the calling thread. Ignored when `atomic_goal` is set.
    formalism::GroundAtomList<formalism::FluentTag> atomic_goal_atoms;

    AtomicGoalPortfolioSearchMode search_mode = AtomicGoalPortfolioSearchMode::INHERIT_CONTEXT;

    /// @brief Number of Rollout IW workers. Zero runs the certifier alone, which is the reference
    /// behaviour every parallel configuration must agree with.
    uint32_t num_rollout_workers = 4;

    /// @brief Worker threads; 0 means "as many as the hardware has". One means run every worker
    /// serially on the calling thread, in order, which makes results fully reproducible.
    uint32_t num_threads = 0;

    uint64_t base_seed = 0;
    uint32_t max_time_in_ms = std::numeric_limits<uint32_t>::max();
    uint64_t max_total_expansions = std::numeric_limits<uint64_t>::max();
    uint32_t max_depth = std::numeric_limits<uint32_t>::max();

    /// @brief One configuration per rollout worker, cycled if shorter than `num_rollout_workers`.
    /// Empty means a built-in mix of goal-directed and randomized guidance with per-worker seeds.
    std::vector<rollout_iw::ActionOrderingConfiguration> rollout_orderings;

    AtomicGoalPortfolioOptions() = default;
};

struct AtomicGoalPortfolioResult
{
    SearchStatus status = SearchStatus::IN_PROGRESS;

    /// @brief The winning plan, re-grounded in the caller's problem after every worker joined.
    std::optional<Plan> plan = std::nullopt;

    /// @brief The same plan in its portable schema/binding form.
    rollout_iw::PlanStepList plan_steps;

    uint32_t plan_length = std::numeric_limits<uint32_t>::max();

    /// @brief Whether the plan is proven shortest. True when the certifier found it itself, or when
    /// the certifier completed depth `plan_length - 1` without finding a goal there. False when the
    /// run stopped on a budget -- an unfinished certifier proves nothing -- and false when the
    /// certifier searched its whole space without finding any plan while a rollout worker did, since
    /// that plan then lies outside the space the certificate is about.
    ///
    /// "Shortest" is relative to the width-1 pruned space the certifier searches. For a goal of
    /// width at most 1 -- the regime this portfolio is built for -- that space contains an optimal
    /// plan, so the claim is optimality outright. For a goal of higher width, including any
    /// conjunctive goal and plenty of single atoms, it is the weaker claim: novelty pruning keeps
    /// the node that first makes an atom true, but only if that node is generated at all, which
    /// needs its whole ancestry to have survived pruning too.
    bool certified_optimal = false;

    /// @brief Monotone lower bound on the optimal plan length: `iw_completed_depth + 1`, or 0.
    uint32_t iw_lower_bound = 0;

    /// @brief The deepest g-layer the certifier finished, or `UINT32_MAX` if it finished none.
    uint32_t iw_completed_depth = std::numeric_limits<uint32_t>::max();

    /// @brief Worker that published the winning plan: 0 is the certifier, 1..K the rollout workers.
    uint32_t winning_worker = std::numeric_limits<uint32_t>::max();

    AtomicGoalPortfolioSearchMode executed_mode = AtomicGoalPortfolioSearchMode::INHERIT_CONTEXT;

    std::string stop_reason;

    SearchStatus certifier_status = SearchStatus::IN_PROGRESS;
    brfs::Statistics iw_statistics;
    std::vector<rollout_iw::Statistics> rollout_statistics;
    std::vector<SearchStatus> rollout_statuses;

    uint64_t total_expansions = 0;
};

/// @brief Run the portfolio over `context`.
///
/// The given context is the caller's and stays untouched while the workers run: they get their own
/// state repositories (grounded mode) or their own grounding overlays (lifted mode). The caller's
/// problem only grows at the very end, when the winning schema/binding sequence is re-grounded into
/// it to build the returned `Plan` -- by which point every worker has joined and no overlay index is
/// resolved again.
///
/// Everything that can be rejected is rejected on the calling thread, before any task is spawned,
/// so a misconfiguration is an exception rather than an abort inside a worker.
extern AtomicGoalPortfolioResult find_solution_atomic_goal_portfolio(const SearchContext& context,
                                                                     const AtomicGoalPortfolioOptions& options = AtomicGoalPortfolioOptions());

}

#endif
