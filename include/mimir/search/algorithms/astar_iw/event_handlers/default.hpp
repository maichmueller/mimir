#ifndef MIMIR_SEARCH_ALGORITHMS_ASTAR_IW_EVENT_HANDLERS_DEFAULT_HPP_
#define MIMIR_SEARCH_ALGORITHMS_ASTAR_IW_EVENT_HANDLERS_DEFAULT_HPP_

#include "mimir/search/algorithms/astar_iw/event_handlers/interface.hpp"

namespace mimir::search::astar_iw
{

class DefaultEventHandlerImpl : public EventHandlerBase<DefaultEventHandlerImpl>
{
public:
    explicit DefaultEventHandlerImpl(formalism::Problem problem, bool quiet = true);
    static DefaultEventHandler create(formalism::Problem problem, bool quiet = true);

    void on_start_search_impl(const State&, ContinuousCost, ContinuousCost);
    void on_generate_state_impl(const State&, formalism::GroundAction, ContinuousCost, const State&);
    void on_expand_state_impl(const State&);
    void on_expand_goal_state_impl(const State&);
    void on_reopen_state_impl(const State&);
    void on_deadend_state_impl(const State&);
    void on_reject_state_novelty_impl(const State&);
    void on_discard_stale_g_impl(const State&);
    void on_discard_stale_novelty_impl(const State&);
    void on_end_search_impl(uint64_t, uint64_t);
    void on_solved_impl(const Plan&);
    void on_unsolvable_impl();
    void on_exhausted_impl();
};

}

#endif
