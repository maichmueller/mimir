#include "mimir/search/algorithms/astar_iw/event_handlers/default.hpp"

#include "mimir/search/plan.hpp"

#include <iostream>

namespace mimir::search::astar_iw
{

DefaultEventHandlerImpl::DefaultEventHandlerImpl(formalism::Problem problem, bool quiet) : EventHandlerBase(std::move(problem), quiet) {}

DefaultEventHandler DefaultEventHandlerImpl::create(formalism::Problem problem, bool quiet)
{
    return std::make_shared<DefaultEventHandlerImpl>(std::move(problem), quiet);
}

void DefaultEventHandlerImpl::on_start_search_impl(const State&, ContinuousCost g, ContinuousCost f)
{
    std::cout << "[AStarIW] Search started with g=" << g << " and f=" << f << std::endl;
}
void DefaultEventHandlerImpl::on_generate_state_impl(const State&, formalism::GroundAction, ContinuousCost, const State&) {}
void DefaultEventHandlerImpl::on_expand_state_impl(const State&) {}
void DefaultEventHandlerImpl::on_expand_goal_state_impl(const State&) {}
void DefaultEventHandlerImpl::on_reopen_state_impl(const State&) {}
void DefaultEventHandlerImpl::on_deadend_state_impl(const State&) {}
void DefaultEventHandlerImpl::on_reject_state_novelty_impl(const State&) {}
void DefaultEventHandlerImpl::on_discard_stale_g_impl(const State&) {}
void DefaultEventHandlerImpl::on_discard_stale_novelty_impl(const State&) {}
void DefaultEventHandlerImpl::on_end_search_impl(uint64_t num_states, uint64_t num_nodes)
{
    std::cout << "[AStarIW] Search ended with " << num_states << " states and " << num_nodes << " nodes." << std::endl;
}
void DefaultEventHandlerImpl::on_solved_impl(const Plan& plan) { std::cout << "[AStarIW] Plan found with cost " << plan.get_cost() << "." << std::endl; }
void DefaultEventHandlerImpl::on_unsolvable_impl() { std::cout << "[AStarIW] Unsolvable." << std::endl; }
void DefaultEventHandlerImpl::on_exhausted_impl() { std::cout << "[AStarIW] Exhausted." << std::endl; }

}
