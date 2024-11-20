
#ifndef PYMIMIR_TRAMPOLINES_HPP
#define PYMIMIR_TRAMPOLINES_HPP

#include <pybind11/pybind11.h>
#include "mimir/mimir.hpp"

using namespace mimir;

/**
 * IPyHeuristic
 *
 * Source: https://pybind11.readthedocs.io/en/stable/advanced/classes.html#overriding-virtual-functions-in-python
 */

class IPyHeuristic : public IHeuristic
{
public:
    /* Inherit the constructors */
    using IHeuristic::IHeuristic;

    /* Trampoline (need one for each virtual function) */
    double compute_heuristic(State state) override
    {
        PYBIND11_OVERRIDE_PURE(double,            /* Return type */
                               IHeuristic,        /* Parent class */
                               compute_heuristic, /* Name of function in C++ (must match Python name) */
                               state              /* Argument(s) */
        );
    }
};

/**
 * IPyDynamicAStarAlgorithmEventHandlerBase
 *
 * Source: https://pybind11.readthedocs.io/en/stable/advanced/classes.html#overriding-virtual-functions-in-python
 */

class IPyDynamicAStarAlgorithmEventHandlerBase : public DynamicAStarAlgorithmEventHandlerBase
{
public:
    /* Inherit the constructors */
    using DynamicAStarAlgorithmEventHandlerBase::DynamicAStarAlgorithmEventHandlerBase;

    /* Trampoline (need one for each virtual function) */
    void on_expand_state_impl(State state, Problem problem, const PDDLRepositories& pddl_repositories) override
    {
        PYBIND11_OVERRIDE(void, DynamicAStarAlgorithmEventHandlerBase, on_expand_state_impl, state, problem, std::cref(pddl_repositories));
    }

    void on_generate_state_impl(State state, GroundAction action, Problem problem, const PDDLRepositories& pddl_repositories) override
    {
        PYBIND11_OVERRIDE(void, DynamicAStarAlgorithmEventHandlerBase, on_generate_state_impl, state, action, problem, std::cref(pddl_repositories));
    }
    void on_generate_state_relaxed_impl(State state, GroundAction action, Problem problem, const PDDLRepositories& pddl_repositories) override
    {
        PYBIND11_OVERRIDE(void, DynamicAStarAlgorithmEventHandlerBase, on_generate_state_relaxed_impl, state, action, problem, std::cref(pddl_repositories));
    }
    void on_generate_state_not_relaxed_impl(State state, GroundAction action, Problem problem, const PDDLRepositories& pddl_repositories) override
    {
        PYBIND11_OVERRIDE(void, DynamicAStarAlgorithmEventHandlerBase, on_generate_state_not_relaxed_impl, state, action, problem, std::cref(pddl_repositories));
    }
    void on_close_state_impl(State state, Problem problem, const PDDLRepositories& pddl_repositories) override
    {
        PYBIND11_OVERRIDE(void, DynamicAStarAlgorithmEventHandlerBase, on_close_state_impl, state, problem, std::cref(pddl_repositories));
    }
    void on_finish_f_layer_impl(double f_value, uint64_t num_expanded_state, uint64_t num_generated_states) override
    {
        PYBIND11_OVERRIDE(void, DynamicAStarAlgorithmEventHandlerBase, on_finish_f_layer_impl, f_value, num_expanded_state, num_generated_states);
    }
    void on_prune_state_impl(State state, Problem problem, const PDDLRepositories& pddl_repositories) override
    {
        PYBIND11_OVERRIDE(void, DynamicAStarAlgorithmEventHandlerBase, on_prune_state_impl, state, problem, std::cref(pddl_repositories));
    }
    void on_start_search_impl(State start_state, Problem problem, const PDDLRepositories& pddl_repositories) override
    {
        PYBIND11_OVERRIDE(void, DynamicAStarAlgorithmEventHandlerBase, on_start_search_impl, start_state, problem, std::cref(pddl_repositories));
    }
    /**
     * Note the trailing commas in the PYBIND11_OVERRIDE calls to name() and bark(). These are needed to portably implement a trampoline for a function that
     * does not take any arguments. For functions that take a nonzero number of arguments, the trailing comma must be omitted.
     */
    void on_end_search_impl() override { PYBIND11_OVERRIDE(void, DynamicAStarAlgorithmEventHandlerBase, on_end_search_impl, ); }
    void on_solved_impl(const GroundActionList& ground_action_plan, const PDDLRepositories& pddl_repositories) override
    {
        PYBIND11_OVERRIDE(void, DynamicAStarAlgorithmEventHandlerBase, on_solved_impl, ground_action_plan, pddl_repositories);
    }
    void on_unsolvable_impl() override { PYBIND11_OVERRIDE(void, DynamicAStarAlgorithmEventHandlerBase, on_unsolvable_impl, ); }
    void on_exhausted_impl() override { PYBIND11_OVERRIDE(void, DynamicAStarAlgorithmEventHandlerBase, on_exhausted_impl, ); }
};


#endif //PYMIMIR_TRAMPOLINES_HPP
