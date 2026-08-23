#ifndef MIMIR_SEARCH_ALGORITHMS_ASTAR_IW_EVENT_HANDLERS_INTERFACE_HPP_
#define MIMIR_SEARCH_ALGORITHMS_ASTAR_IW_EVENT_HANDLERS_INTERFACE_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/search/algorithms/astar_iw/event_handlers/statistics.hpp"
#include "mimir/search/declarations.hpp"

#include <cstdint>
#include <utility>

namespace mimir::search::astar_iw
{

class IEventHandler
{
public:
    virtual ~IEventHandler() = default;
    virtual void on_start_search(const State& state, ContinuousCost g_value, ContinuousCost f_value) = 0;
    virtual void on_generate_state(const State& state, formalism::GroundAction action, ContinuousCost action_cost, const State& successor_state) = 0;
    virtual void on_expand_state(const State& state) = 0;
    virtual void on_expand_goal_state(const State& state) = 0;
    virtual void on_reopen_state(const State& state) = 0;
    virtual void on_deadend_state(const State& state) = 0;
    virtual void on_reject_state_novelty(const State& state) = 0;
    virtual void on_discard_stale_g(const State& state) = 0;
    virtual void on_discard_stale_novelty(const State& state) = 0;
    virtual void on_end_search(uint64_t num_states, uint64_t num_nodes) = 0;
    virtual void on_solved(const Plan& plan) = 0;
    virtual void on_unsolvable() = 0;
    virtual void on_exhausted() = 0;
    virtual const Statistics& get_statistics() const = 0;
};

template<typename Derived>
class EventHandlerBase : public IEventHandler
{
protected:
    Statistics m_statistics;
    formalism::Problem m_problem;
    bool m_quiet;

    Derived& self() { return static_cast<Derived&>(*this); }

public:
    explicit EventHandlerBase(formalism::Problem problem, bool quiet = true) : m_problem(std::move(problem)), m_quiet(quiet) {}

    void on_start_search(const State& state, ContinuousCost g_value, ContinuousCost f_value) override
    {
        m_statistics.reset();
        m_statistics.start();
        if (!m_quiet)
            self().on_start_search_impl(state, g_value, f_value);
    }
    void on_generate_state(const State& state, formalism::GroundAction action, ContinuousCost action_cost, const State& successor_state) override
    {
        m_statistics.increment_num_generated();
        if (!m_quiet)
            self().on_generate_state_impl(state, action, action_cost, successor_state);
    }
    void on_expand_state(const State& state) override
    {
        m_statistics.increment_num_expanded();
        if (!m_quiet)
            self().on_expand_state_impl(state);
    }
    void on_expand_goal_state(const State& state) override
    {
        if (!m_quiet)
            self().on_expand_goal_state_impl(state);
    }
    void on_reopen_state(const State& state) override
    {
        m_statistics.increment_num_reopened();
        if (!m_quiet)
            self().on_reopen_state_impl(state);
    }
    void on_deadend_state(const State& state) override
    {
        m_statistics.increment_num_deadends();
        if (!m_quiet)
            self().on_deadend_state_impl(state);
    }
    void on_reject_state_novelty(const State& state) override
    {
        m_statistics.increment_num_novelty_rejected();
        if (!m_quiet)
            self().on_reject_state_novelty_impl(state);
    }
    void on_discard_stale_g(const State& state) override
    {
        m_statistics.increment_num_stale_g_discarded();
        if (!m_quiet)
            self().on_discard_stale_g_impl(state);
    }
    void on_discard_stale_novelty(const State& state) override
    {
        m_statistics.increment_num_stale_novelty_discarded();
        if (!m_quiet)
            self().on_discard_stale_novelty_impl(state);
    }
    void on_end_search(uint64_t num_states, uint64_t num_nodes) override
    {
        m_statistics.stop();
        m_statistics.set_num_states(num_states);
        m_statistics.set_num_nodes(num_nodes);
        if (!m_quiet)
            self().on_end_search_impl(num_states, num_nodes);
    }
    void on_solved(const Plan& plan) override
    {
        if (!m_quiet)
            self().on_solved_impl(plan);
    }
    void on_unsolvable() override
    {
        if (!m_quiet)
            self().on_unsolvable_impl();
    }
    void on_exhausted() override
    {
        if (!m_quiet)
            self().on_exhausted_impl();
    }
    const Statistics& get_statistics() const override { return m_statistics; }
};

}

#endif
