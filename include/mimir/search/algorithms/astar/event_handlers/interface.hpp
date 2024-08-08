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

#ifndef MIMIR_SEARCH_ALGORITHMS_ASTAR_EVENT_HANDLERS_INTERFACE_HPP_
#define MIMIR_SEARCH_ALGORITHMS_ASTAR_EVENT_HANDLERS_INTERFACE_HPP_

#include "mimir/formalism/declarations.hpp"
#include "mimir/search/action.hpp"
#include "mimir/search/algorithms/astar/event_handlers/statistics.hpp"
#include "mimir/search/search_node.hpp"
#include "mimir/search/state.hpp"

#include <chrono>
#include <concepts>

namespace mimir
{

/**
 * Interface class
 */

/// @brief `IAStarAlgorithmEventHandler` to react on event during AStar search.
///
/// Inspired by boost graph library: https://www.boost.org/doc/libs/1_75_0/libs/graph/doc/AStarVisitor.html
class IAStarAlgorithmEventHandler
{
public:
    virtual ~IAStarAlgorithmEventHandler() = default;

    /// @brief React on expanding a state.
    /// This is happens immediately before on_generate_state for successors of `state`.
    virtual void on_expand_state(State state, ConstSearchNode<double, double> search_node, Problem problem, const PDDLFactories& pddl_factories) = 0;

    /// @brief React on generating a successor_state by applying an action.
    virtual void on_generate_state(State state, ConstSearchNode<double, double> search_node, Problem problem, const PDDLFactories& pddl_factories) = 0;

    /// @brief React on generating a relaxed successor state by applying an action where
    /// a successor state is relaxed if the f value decreases.
    virtual void on_generate_state_relaxed(State state, ConstSearchNode<double, double> search_node, Problem problem, const PDDLFactories& pddl_factories) = 0;

    /// @brief React on generated an unrelaxed successor state by applying an action.
    virtual void
    on_generate_state_not_relaxed(State state, ConstSearchNode<double, double> search_node, Problem problem, const PDDLFactories& pddl_factories) = 0;

    virtual void on_close_state(State state, ConstSearchNode<double, double> search_node, Problem problem, const PDDLFactories& pddl_factories) = 0;

    /// @brief React on finishing expanding a g-layer.
    virtual void on_finish_g_layer() = 0;

    /// @brief React on pruning a state.
    virtual void on_prune_state(State state, Problem problem, const PDDLFactories& pddl_factories) = 0;

    /// @brief React on starting a search.
    virtual void on_start_search(State initial_state, Problem problem, const PDDLFactories& pddl_factories) = 0;

    /// @brief React on ending a search.
    virtual void on_end_search() = 0;

    /// @brief React on solving a search.
    virtual void on_solved(const GroundActionList& ground_action_plan) = 0;

    /// @brief React on proving unsolvability during a search.
    virtual void on_unsolvable() = 0;

    /// @brief React on exhausting a search.
    virtual void on_exhausted() = 0;

    virtual const AStarAlgorithmStatistics& get_statistics() const = 0;
    virtual bool is_quiet() const = 0;
};

/**
 * Base class
 *
 * Collect statistics and call implementation of derived class.
 */
template<typename Derived_>
class AStarAlgorithmEventHandlerBase : public IAStarAlgorithmEventHandler
{
protected:
    AStarAlgorithmStatistics m_statistics;
    bool m_quiet;

private:
    AStarAlgorithmEventHandlerBase() = default;
    friend Derived_;

    /// @brief Helper to cast to Derived.
    constexpr const auto& self() const { return static_cast<const Derived_&>(*this); }
    constexpr auto& self() { return static_cast<Derived_&>(*this); }

public:
    explicit AStarAlgorithmEventHandlerBase(bool quiet = true) : m_statistics(), m_quiet(quiet) {}

    void on_expand_state(State state, ConstSearchNode<double, double> search_node, Problem problem, const PDDLFactories& pddl_factories) override
    {
        m_statistics.increment_num_expanded();

        if (!m_quiet)
        {
            self().on_expand_state_impl(state, search_node, problem, pddl_factories);
        }
    }

    void on_generate_state(State state, ConstSearchNode<double, double> search_node, Problem problem, const PDDLFactories& pddl_factories) override
    {
        m_statistics.increment_num_generated();

        if (!m_quiet)
        {
            self().on_generate_state_impl(state, search_node, problem, pddl_factories);
        }
    }

    void on_generate_state_relaxed(State state, ConstSearchNode<double, double> search_node, Problem problem, const PDDLFactories& pddl_factories) override
    {
        if (!m_quiet)
        {
            self().on_generate_state_relaxed_impl(state, search_node, problem, pddl_factories);
        }
    }

    void on_generate_state_not_relaxed(State state, ConstSearchNode<double, double> search_node, Problem problem, const PDDLFactories& pddl_factories) override
    {
        if (!m_quiet)
        {
            self().on_generate_state_relaxed_impl(state, search_node, problem, pddl_factories);
        }
    }

    void on_close_state(State state, ConstSearchNode<double, double> search_node, Problem problem, const PDDLFactories& pddl_factories) override
    {
        if (!m_quiet)
        {
            self().on_close_state_impl(state, search_node, problem, pddl_factories);
        }
    }

    void on_finish_g_layer() override
    {
        m_statistics.on_finish_g_layer();

        if (!m_quiet)
        {
            assert(!m_statistics.get_num_expanded_until_f_value().empty());
            self().on_finish_g_layer_impl(m_statistics.get_num_expanded_until_f_value().size() - 1,
                                          m_statistics.get_num_expanded_until_f_value().back(),
                                          m_statistics.get_num_generated_until_f_value().back());
        }
    }

    void on_prune_state(State state, Problem problem, const PDDLFactories& pddl_factories) override
    {
        m_statistics.increment_num_pruned();

        if (!m_quiet)
        {
            self().on_prune_state_impl(state, problem, pddl_factories);
        }
    }

    void on_start_search(State initial_state, Problem problem, const PDDLFactories& pddl_factories) override
    {
        m_statistics = AStarAlgorithmStatistics();

        m_statistics.set_search_start_time_point(std::chrono::high_resolution_clock::now());

        if (!m_quiet)
        {
            self().on_start_search_impl(initial_state, problem, pddl_factories);
        }
    }

    void on_end_search() override
    {
        m_statistics.set_search_end_time_point(std::chrono::high_resolution_clock::now());

        if (!m_quiet)
        {
            self().on_end_search_impl();
        }
    }

    void on_solved(const GroundActionList& ground_action_plan) override
    {
        if (!m_quiet)
        {
            self().on_solved_impl(ground_action_plan);
        }
    }

    void on_unsolvable() override
    {
        if (!m_quiet)
        {
            self().on_unsolvable_impl();
        }
    }

    void on_exhausted() override
    {
        if (!m_quiet)
        {
            self().on_exhausted_impl();
        }
    }

    /// @brief Get the statistics.
    const AStarAlgorithmStatistics& get_statistics() const override { return m_statistics; }
    bool is_quiet() const override { return m_quiet; }
};

}

#endif
