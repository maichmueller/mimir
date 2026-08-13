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

#include "mimir/search/algorithms/iw.hpp"

#include "mimir/formalism/problem.hpp"
#include "mimir/search/algorithms/brfs.hpp"
#include "mimir/search/algorithms/brfs/event_handlers.hpp"
#include "mimir/search/algorithms/iw/event_handlers.hpp"
#include "mimir/search/algorithms/iw/event_handlers/interface.hpp"
#include "mimir/search/algorithms/iw/pruning_strategy.hpp"
#include "mimir/search/algorithms/strategies/goal_strategy.hpp"
#include "mimir/search/algorithms/strategies/transition_ordering_strategy.hpp"
#include "mimir/search/algorithms/utils.hpp"
#include "mimir/search/applicable_action_generators/interface.hpp"
#include "mimir/search/axiom_evaluators/interface.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

#include <chrono>
#include <exception>
#include <limits>
#include <utility>

using namespace mimir::formalism;

namespace mimir::search::iw
{
namespace
{
/// @brief Emits `IEventHandler::on_end_search` exactly once for an IW search that has started,
/// whichever arity pass it stops in and for whatever reason. See `brfs::SearchEndGuard` for the
/// same obligation on the BrFS side.
class SearchEndGuard
{
public:
    explicit SearchEndGuard(EventHandler event_handler) : m_event_handler(std::move(event_handler)), m_finished(false) {}
    SearchEndGuard(const SearchEndGuard&) = delete;
    SearchEndGuard& operator=(const SearchEndGuard&) = delete;

    ~SearchEndGuard() noexcept(false)
    {
        if (std::uncaught_exceptions() > 0)
        {
            /* Already unwinding: a second exception from here would terminate the process. */
            try
            {
                finish();
            }
            catch (...)
            {
            }
            return;
        }

        finish();
    }

    /// @brief Emit the event now instead of at scope exit, so a path that reports more afterwards
    /// keeps its ordering. Any call after the first is a no-op.
    void finish()
    {
        if (m_finished)
        {
            return;
        }
        m_finished = true;
        m_event_handler->on_end_search();
    }

private:
    EventHandler m_event_handler;
    bool m_finished;
};
}

template<TransitionOrderingStrategy Ordering = QueuedTransitionOrderingStrategy>
SearchResult find_solution_impl(const SearchContext& context, const Options& options, const Ordering& ordering = {})
{
    auto& applicable_action_generator = *context->get_applicable_action_generator();
    auto& state_repository = *context->get_state_repository();

    const auto max_arity = options.max_arity;
    const auto [start_state, start_g_value] = (options.start_state) ?
                                                  std::make_pair(options.start_state.value(), compute_state_metric_value(options.start_state.value())) :
                                                  state_repository.get_or_create_initial_state();
    const auto iw_event_handler = (options.iw_event_handler) ? options.iw_event_handler : DefaultEventHandlerImpl::create(context->get_problem());
    const auto brfs_event_handler = (options.brfs_event_handler) ? options.brfs_event_handler : brfs::DefaultEventHandlerImpl::create(context->get_problem());
    const auto goal_strategy = (options.goal_strategy) ? options.goal_strategy : ProblemGoalStrategyImpl::create(context->get_problem());

    if (max_arity >= MAX_ARITY)
    {
        throw std::runtime_error("iw::find_solution(...): max_arity (" + std::to_string(max_arity) + ") cannot be greater than or equal to MAX_ARITY ("
                                 + std::to_string(MAX_ARITY) + ") compile time constant.");
    }

    iw_event_handler->on_start_search(start_state);

    /* Every path out of here from now on owes the handler its end-of-search report. */
    auto end_guard = SearchEndGuard(iw_event_handler);

    /* The time budget spans the whole search, not each arity pass, so every pass is handed what is
       left of it. Passing `options.max_time_in_ms` to each pass instead would let a `max_arity` of
       k take k times as long as asked for. */
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.max_time_in_ms);
    const auto remaining_time_in_ms = [&]() -> uint32_t
    {
        if (options.max_time_in_ms == std::numeric_limits<uint32_t>::max())
        {
            return std::numeric_limits<uint32_t>::max();
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        return (remaining <= 0) ? 0u : static_cast<uint32_t>(remaining);
    };

    const auto& ground_fluent_atom_repository =
        boost::hana::at_key(context->get_problem()->get_repositories().get_hana_repositories(), boost::hana::type<GroundAtomImpl<FluentTag>> {});

    const auto optimize_iw1_root_actions = (max_arity == 1);
    if (optimize_iw1_root_actions)
    {
        // Optimized IW(1) skips the standalone width-0 BrFS run and lets the width-1
        // pruning strategy admit all root successors once while deciding continuation
        // from actual width-1 novelty. Keep a placeholder entry so per-arity statistics
        // still line up with width indices.
        iw_event_handler->on_start_arity_search(start_state, 0);
        iw_event_handler->on_end_arity_search(brfs::Statistics());
    }

    size_t cur_arity = optimize_iw1_root_actions ? 1 : 0;
    while (cur_arity <= max_arity)
    {
        /* Check before starting the pass, not just inside it. Building a width-k novelty table is
           O(|atoms|^k) and happens before the search loop ever looks at a clock, so entering a pass
           with no budget left can cost seconds of uninterruptible setup for a search that is going
           to stop on its first node. */
        if (remaining_time_in_ms() == 0)
        {
            auto result = SearchResult();
            result.status = SearchStatus::OUT_OF_TIME;
            return result;
        }

        if (options.control && options.control->is_canceled())
        {
            auto result = SearchResult();
            result.status = SearchStatus::CANCELED;
            return result;
        }

        iw_event_handler->on_start_arity_search(start_state, cur_arity);

        const auto use_iw1_specific_options = (cur_arity == 1);

        auto options_i = brfs::Options();
        options_i.start_state = start_state;
        options_i.event_handler = brfs_event_handler;
        options_i.goal_strategy = goal_strategy;
        options_i.layer_ordering_strategy = options.layer_ordering_strategy;
        options_i.max_next_layer_states = options.max_next_layer_states;
        options_i.beam_width = options.beam_width;
        options_i.beam_novelty_mode = options.beam_novelty_mode;
        options_i.relaxed_survivors_only_beam = options.relaxed_survivors_only_beam;
        options_i.randomize_equal_score_ties = options.randomize_equal_score_ties;
        options_i.equal_score_tie_seed = options.equal_score_tie_seed;
        options_i.parallel_beam_num_threads = options.parallel_beam_num_threads;
        options_i.parallel_beam_chunk_size = options.parallel_beam_chunk_size;
        // These controls are width-1-specific and must not be applied to the arity-0
        // warm-up pass or any wider IW(k) pass.
        options_i.iw1_precheck_add_effect_novelty = use_iw1_specific_options && options.iw1_precheck_add_effect_novelty;
        options_i.iw1_atom_first_mode = use_iw1_specific_options && options.iw1_atom_first_mode;
        options_i.iw1_atom_first_ratio = options.iw1_atom_first_ratio;
        options_i.iw1_incremental_first_applicability =
            use_iw1_specific_options && options.iw1_incremental_first_applicability;
        options_i.iw1_incremental_first_applicability_debug_crosscheck =
            use_iw1_specific_options && options.iw1_incremental_first_applicability_debug_crosscheck;
        options_i.max_depth = options.max_depth;
        options_i.max_time_in_ms = remaining_time_in_ms();
        options_i.max_num_states = options.max_num_states;
        options_i.control = options.control;
        options_i.pruning_strategy = (cur_arity > 0) ? ArityKNoveltyPruningStrategyImpl::create(cur_arity,
                                                                                                  ground_fluent_atom_repository.size(),
                                                                                                  optimize_iw1_root_actions && (cur_arity == 1)) :
                                                       ArityZeroNoveltyPruningStrategyImpl::create(start_state);

        auto result = SearchResult();
        if constexpr (Ordering::requires_deferred_novelty)
        {
            // Landmark ordering only applies to the width-1 pass; arity 0 and arity > 1 stay queued.
            result = (cur_arity == 1) ? brfs::find_solution(context, options_i, ordering) : brfs::find_solution(context, options_i);
        }
        else
        {
            result = brfs::find_solution(context, options_i);
        }

        iw_event_handler->on_end_arity_search(brfs_event_handler->get_statistics());

        if (result.status == SearchStatus::SOLVED)
        {
            end_guard.finish();
            if (!iw_event_handler->is_quiet())
            {
                applicable_action_generator.on_end_search();
                state_repository.get_axiom_evaluator()->on_end_search();
            }
            iw_event_handler->on_solved(result.plan.value());

            return result;
        }
        else if (result.status == SearchStatus::UNSOLVABLE)
        {
            end_guard.finish();
            iw_event_handler->on_unsolvable();

            return result;
        }
        else if (result.status == SearchStatus::OUT_OF_TIME || result.status == SearchStatus::OUT_OF_STATES || result.status == SearchStatus::CANCELED)
        {
            /* The pass did not fail to find a plan at this width, it stopped before it could tell.
               Escalating to the next arity would spend the budget we just ran out of. */
            return result;
        }

        ++cur_arity;
    }

    auto result = SearchResult();
    result.status = SearchStatus::FAILED;
    return result;
}

SearchResult find_solution(const SearchContext& context, const Options& options) { return find_solution_impl(context, options); }

SearchResult find_solution(const SearchContext& context, const Options& options, const LandmarkTransitionOrderingStrategy& ordering)
{
    return find_solution_impl(context, options, ordering);
}
}
