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

#include "incremental_iw1.hpp"
#include "internal.hpp"

#include "mimir/formalism/formatter.hpp"
#include "mimir/formalism/problem.hpp"
#include "mimir/search/algorithms/brfs/event_handlers/interface.hpp"

#include <sstream>
#include <unordered_set>

using namespace mimir::formalism;

namespace mimir::search::brfs
{
namespace
{
std::string format_ground_action(GroundAction action, Problem problem)
{
    if (!action)
    {
        return "<invalid-action>";
    }

    auto out = std::ostringstream {};
    out << std::tuple<const GroundActionImpl&, const ProblemImpl&, PlanFormatterTag> { *action, *problem, PlanFormatterTag {} };
    return out.str();
}

std::string format_ground_action_list(const std::span<const GroundAction>& actions, Problem problem)
{
    auto out = std::ostringstream {};
    out << "[";
    for (size_t i = 0; i < actions.size(); ++i)
    {
        if (i != 0)
        {
            out << ", ";
        }
        out << format_ground_action(actions[i], problem);
    }
    out << "]";
    return out.str();
}
}

IW1IncrementalStatisticsReporter::~IW1IncrementalStatisticsReporter()
{
    if (event_handler && controller)
    {
        event_handler->on_finish_iw1_incremental_first_applicability(controller->get_statistics());
    }
}

void run_incremental_precheck_filtered_crosscheck(const SearchContext& context,
                                                  const Options& options,
                                                  const State& start_state,
                                                  const State& state,
                                                  const PruningStrategy& pruning_strategy,
                                                  StateRepositoryImpl& state_repository,
                                                  const IW1IncrementalActionDiscoveryController& iw1_incremental_action_discovery,
                                                  const std::span<const GroundAction>& filtered_incremental_actions)
{
    auto baseline_never_tested = std::vector<GroundAction> {};
    for (const auto action : context->get_applicable_action_generator()->create_applicable_action_generator(state))
    {
        if (!iw1_incremental_action_discovery.has_tested_action(action))
        {
            baseline_never_tested.push_back(action);
        }
    }

    auto debug_precheck = IW1ActionPrecheckController(options, pruning_strategy, context->get_problem(), start_state);
    const auto filtered_baseline_actions = debug_precheck.filter_actions(state, baseline_never_tested, state_repository);

    auto filtered_incremental_indices = std::unordered_set<Index> {};
    for (const auto action : filtered_incremental_actions)
    {
        filtered_incremental_indices.insert(action->get_index());
    }

    auto baseline_indices = std::unordered_set<Index> {};
    auto missing_actions = std::vector<GroundAction> {};
    auto spurious_actions = std::vector<GroundAction> {};

    for (const auto action : filtered_baseline_actions)
    {
        baseline_indices.insert(action->get_index());
        if (!filtered_incremental_indices.contains(action->get_index()))
        {
            missing_actions.push_back(action);
        }
    }

    for (const auto action : filtered_incremental_actions)
    {
        if (!baseline_indices.contains(action->get_index()))
        {
            spurious_actions.push_back(action);
        }
    }

    if (missing_actions.empty() && spurious_actions.empty())
    {
        return;
    }

    auto message = std::ostringstream {};
    message << "IW(1) incremental first-applicability filtered precheck cross-check failed.\n";
    message << "state_id: " << state.get_index() << "\n";
    message << "filtered_incremental_candidates: " << format_ground_action_list(filtered_incremental_actions, context->get_problem()) << "\n";
    message << "filtered_baseline_candidates: " << format_ground_action_list(filtered_baseline_actions, context->get_problem()) << "\n";
    message << "missing_actions: " << format_ground_action_list(missing_actions, context->get_problem()) << "\n";
    message << "spurious_actions: " << format_ground_action_list(spurious_actions, context->get_problem()) << "\n";
    throw std::runtime_error(message.str());
}
}
