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
#include "mimir/search/algorithms/utils.hpp"
#include "mimir/search/applicable_action_generators/interface.hpp"
#include "mimir/search/axiom_evaluators/interface.hpp"
#include "mimir/search/search_context.hpp"
#include "mimir/search/state_repository.hpp"

using namespace mimir::formalism;

namespace mimir::search::iw
{
SearchResult find_solution(const SearchContext& context, const Options& options)
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

    const auto& ground_fluent_atom_repository =
        boost::hana::at_key(context->get_problem()->get_repositories().get_hana_repositories(), boost::hana::type<GroundAtomImpl<FluentTag>> {});

    size_t cur_arity = 0;
    while (cur_arity <= max_arity)
    {
        iw_event_handler->on_start_arity_search(start_state, cur_arity);

        auto options_i = brfs::Options();
        options_i.start_state = start_state;
        options_i.event_handler = brfs_event_handler;
        options_i.goal_strategy = goal_strategy;
        options_i.layer_ordering_strategy = options.layer_ordering_strategy;
        options_i.max_next_layer_states = options.max_next_layer_states;
        options_i.beam_width = options.beam_width;
        options_i.beam_novelty_mode = options.beam_novelty_mode;
        options_i.randomize_equal_score_ties = options.randomize_equal_score_ties;
        options_i.equal_score_tie_seed = options.equal_score_tie_seed;
        options_i.parallel_beam_num_threads = options.parallel_beam_num_threads;
        options_i.parallel_beam_chunk_size = options.parallel_beam_chunk_size;
        options_i.pruning_strategy = (cur_arity > 0) ? ArityKNoveltyPruningStrategyImpl::create(cur_arity, ground_fluent_atom_repository.size()) :
                                                       ArityZeroNoveltyPruningStrategyImpl::create(start_state);

        const auto result = brfs::find_solution(context, options_i);

        iw_event_handler->on_end_arity_search(brfs_event_handler->get_statistics());

        if (result.status == SearchStatus::SOLVED)
        {
            iw_event_handler->on_end_search();
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
            iw_event_handler->on_unsolvable();

            return result;
        }

        ++cur_arity;
    }

    auto result = SearchResult();
    result.status = SearchStatus::FAILED;
    return result;
}
}
