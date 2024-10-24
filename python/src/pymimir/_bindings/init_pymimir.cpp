#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;
using namespace mimir;

void init_pymimir(py::module& m)
{
    // order is important either for dependencies in return types of bound functions or for stubgen!
    init_enums(m);
    init_requirements(m);
    init_object(m);
    init_variable(m);
    init_termobject(m);
    init_termvariable(m);
    init_termvariant(m);
    init_predicates(m);
    init_atoms(m);
    init_pddl_factories(m);
    init_ground_atoms(m);
    init_literals(m);
    init_axiom(m);
    init_numeric_fluent(m);
    init_effects(m);
    init_flatbitset(m);
    init_function(m);
    init_function_expression(m);
    init_ground_function_expression(m);
    init_optimization_metric(m);
    init_actions(m);

    init_domain(m);
    init_problem(m);

    py::class_<PDDLParser>(m, "PDDLParser")  //
        .def(py::init<std::string, std::string>(), py::arg("domain_path"), py::arg("problem_path"))
        .def("get_domain", &PDDLParser::get_domain, py::return_value_policy::reference_internal)
        .def("get_problem", &PDDLParser::get_problem, py::return_value_policy::reference_internal)
        .def("get_pddl_factories", &PDDLParser::get_pddl_factories);

    init_state(m);
    init_strips_action_precondition(m);
    init_conditional_effect(m);
    init_aag(m);
    init_heuristics(m);
    init_algorithms(m);
    init_state_space(m);
    init_abstraction(m);
    init_static_digraph(m);
    init_nauty_wrappers(m);
    init_static_vertexcolored_graph(m);
    init_tuple_graph(m);

    py::class_<ObjectGraphPruningStrategy>(m, "ObjectGraphPruningStrategy");

    // ObjectGraph
    m.def("create_object_graph",
          &create_object_graph,
          py::arg("color_function"),
          py::arg("pddl_factories"),
          py::arg("problem"),
          py::arg("state"),
          py::arg("state_index") = 0,
          py::arg("mark_true_goal_literals") = false,
          py::arg("pruning_strategy") = ObjectGraphPruningStrategy(),
          "Creates an object graph based on the provided parameters");
}