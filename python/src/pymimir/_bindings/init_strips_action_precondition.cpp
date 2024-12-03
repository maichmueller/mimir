#include "init_declarations.hpp"
#include "opaque_types.hpp"
#include "pymimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace pymimir;

void init_strips_action_precondition(py::module& m)
{
    class_<GroundConditionStrips>(m, "GroundConditionStrips")
        .def("get_positive_condition", &all_atoms_from_conditions<true, GroundConditionStrips>)
        .def("get_fluent_positive_condition", &atoms_from_conditions<true, Fluent, GroundConditionStrips>)
        .def("get_static_positive_condition", &atoms_from_conditions<true, Static, GroundConditionStrips>)
        .def("get_derived_positive_condition", &atoms_from_conditions<true, Derived, GroundConditionStrips>)
        .def("get_negative_condition", &all_atoms_from_conditions<false, GroundConditionStrips>)
        .def("get_fluent_negative_condition", &atoms_from_conditions<false, Fluent, GroundConditionStrips>)
        .def("get_static_negative_condition", &atoms_from_conditions<false, Static, GroundConditionStrips>)
        .def("get_derived_negative_condition", &atoms_from_conditions<false, Derived, GroundConditionStrips>)
        .def("get_fluent_positive_condition_indices", CONST_OVERLOAD(GroundConditionStrips::get_positive_precondition<Fluent>), py::return_value_policy::copy)
        .def("get_static_positive_condition_indices", CONST_OVERLOAD(GroundConditionStrips::get_positive_precondition<Static>), py::return_value_policy::copy)
        .def("get_derived_positive_condition_indices", CONST_OVERLOAD(GroundConditionStrips::get_positive_precondition<Derived>), py::return_value_policy::copy)
        .def("get_fluent_negative_condition_indices", CONST_OVERLOAD(GroundConditionStrips::get_negative_precondition<Fluent>), py::return_value_policy::copy)
        .def("get_static_negative_condition_indices", CONST_OVERLOAD(GroundConditionStrips::get_negative_precondition<Static>), py::return_value_policy::copy)
        .def("get_derived_negative_condition_indices", CONST_OVERLOAD(GroundConditionStrips::get_negative_precondition<Derived>), py::return_value_policy::copy)
        .def("is_dynamically_applicable", &GroundConditionStrips::is_dynamically_applicable, py::arg("state"))
        .def(
            "is_applicable",
            [](const GroundConditionStrips& self, Problem problem, State state) { return self.is_applicable(problem, state); },
            py::arg("problem"),
            py::arg("state"));
}