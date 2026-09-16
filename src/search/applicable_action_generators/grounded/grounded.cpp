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
 *<
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "mimir/search/applicable_action_generators/grounded/grounded.hpp"

#include "mimir/search/applicability.hpp"
#include "mimir/search/applicable_action_generators/grounded/event_handlers/default.hpp"
#include "mimir/search/applicable_action_generators/grounded/event_handlers/interface.hpp"
#include "mimir/search/match_tree/match_tree.hpp"
#include "mimir/search/state.hpp"
#include "mimir/search/state_repository.hpp"

using namespace mimir::formalism;

namespace mimir::search
{

/**
 * GroundedApplicableActionGenerator
 */

GroundedApplicableActionGeneratorImpl::GroundedApplicableActionGeneratorImpl(Problem problem,
                                                                             match_tree::MatchTree<GroundActionImpl>&& match_tree,
                                                                             EventHandler event_handler) :
    m_problem(std::move(problem)),
    m_match_tree(std::move(match_tree)),
    m_event_handler(std::move(event_handler))
{
}

std::shared_ptr<GroundedApplicableActionGeneratorImpl> GroundedApplicableActionGeneratorImpl::create(Problem problem,
                                                                                                     match_tree::MatchTree<GroundActionImpl>&& match_tree)
{
    return create(problem, std::move(match_tree), DefaultEventHandlerImpl::create());
}

std::shared_ptr<GroundedApplicableActionGeneratorImpl>
GroundedApplicableActionGeneratorImpl::create(Problem problem, match_tree::MatchTree<GroundActionImpl>&& match_tree, EventHandler event_handler)
{
    return std::shared_ptr<GroundedApplicableActionGeneratorImpl>(
        new GroundedApplicableActionGeneratorImpl(std::move(problem), std::move(match_tree), std::move(event_handler)));
}

bool GroundedApplicableActionGeneratorImpl::supports_parallel_beam() const { return true; }

mimir::generator<GroundAction> GroundedApplicableActionGeneratorImpl::create_applicable_action_generator(const State& state)
{
    /* The match tree is walked lazily rather than drained into a list first, so that a caller who
       stops after the first action -- a dead-end test, most of all -- pays for the nodes it reached
       and no more. Exhaustive consumers see the same actions in the same order. */
    auto element_generator = m_match_tree->create_applicable_elements_generator(state.get_unpacked_state());

    for (const auto& ground_action : element_generator)
    {
        assert(is_applicable(ground_action, state));
        co_yield ground_action;
    }
}

bool GroundedApplicableActionGeneratorImpl::has_applicable_action(const State& state)
{
    return m_match_tree->has_applicable_element(state.get_unpacked_state());
}

bool GroundedApplicableActionGeneratorImpl::supports_concurrent_applicable_action_generators() const { return true; }

const Problem& GroundedApplicableActionGeneratorImpl::get_problem() const { return m_problem; }

void GroundedApplicableActionGeneratorImpl::on_finish_search_layer() { m_event_handler->on_finish_search_layer(); }

void GroundedApplicableActionGeneratorImpl::on_end_search() { m_event_handler->on_end_search(); }

}
