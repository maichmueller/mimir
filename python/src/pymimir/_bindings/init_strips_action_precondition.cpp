#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "opaque_types.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;


void init_strips_action_precondition(py::module& m)
{
    py::class_<StripsActionPrecondition>(m, "StripsActionPrecondition")
        .def("get_positive_condition", &all_atoms_from_conditions<true, StripsActionPrecondition>)
        .def("get_fluent_positive_condition", &atoms_from_conditions<true, Fluent, StripsActionPrecondition>)
        .def("get_static_positive_condition", &atoms_from_conditions<true, Static, StripsActionPrecondition>)
        .def("get_derived_positive_condition", &atoms_from_conditions<true, Derived, StripsActionPrecondition>)
        .def("get_negative_condition", &all_atoms_from_conditions<false, StripsActionPrecondition>)
        .def("get_fluent_positive_condition", &atoms_from_conditions<false, Fluent, StripsActionPrecondition>)
        .def("get_static_positive_condition", &atoms_from_conditions<false, Static, StripsActionPrecondition>)
        .def("get_derived_positive_condition", &atoms_from_conditions<false, Derived, StripsActionPrecondition>)
        .def("is_dynamically_applicable", &StripsActionPrecondition::is_dynamically_applicable, py::arg("state"))
        .def(
            "is_applicable",
            [](const StripsActionPrecondition& self, Problem problem, State state) { return self.is_applicable(problem, state); },
            py::arg("problem"),
            py::arg("state"));
}