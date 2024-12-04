#include "init_declarations.hpp"
#include "mimir/common/itertools.hpp"
#include "opaque_types.hpp"
#include "pymimir.hpp"
#include "trampolines.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace pymimir;

void init_pymimir(py::module& m)
{
    // first the cental registrations
    register_classes(m);
    init_enums(m);
    // and the lists
    init_lists(m);
    // now all the individual class detail bindings...
    init_requirements(m);
    init_object(m);
    init_variable(m);
    init_term(m);
    init_predicates(m);
    init_atoms(m);
    init_pddl_repositories(m);
    init_ground_atoms(m);
    init_literals(m);
    init_axiom(m);
    init_numeric_fluent(m);
    init_effects(m);
    init_cista_types(m);
    init_function(m);
    init_function_expression(m);
    init_ground_function_expression(m);
    init_optimization_metric(m);
    init_plan(m);
    init_actions(m);
    init_domain(m);
    init_problem(m);
    init_parser(m);
    init_state(m);
    init_strips_action_precondition(m);
    init_conditional_effect(m);
    init_applicable_action_generator(m);
    init_heuristics(m);
    init_algorithms(m);
    init_state_space(m);
    init_abstraction(m);
    init_static_digraph(m);
    init_nauty_wrappers(m);
    init_static_vertexcolored_graph(m);
    init_tuple_graph(m);
}