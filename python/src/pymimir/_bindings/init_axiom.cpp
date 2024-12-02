#include "init_declarations.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace pymimir;

void init_axiom(py::module& m)
{
    class_<AxiomImpl>(m, "Axiom")  //
        .def("__str__", &AxiomImpl::str)
        .def("__repr__", &AxiomImpl::str)
        .def("get_index", &AxiomImpl::get_index)
        .def("get_literal", &AxiomImpl::get_literal, py::return_value_policy::reference_internal)
        .def(
            "get_static_conditions",
            [](const AxiomImpl& self) { return LiteralList<Static>(self.get_conditions<Static>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_fluent_conditions",
            [](const AxiomImpl& self) { return LiteralList<Fluent>(self.get_conditions<Fluent>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_derived_conditions",
            [](const AxiomImpl& self) { return LiteralList<Derived>(self.get_conditions<Derived>()); },
            py::keep_alive<0, 1>())
        .def("get_arity", &AxiomImpl::get_arity);

    class_<GroundAxiomImpl>(m, "GroundAxiomImpl")
        .def(py::init<>())  // Default constructor
        .def("get_index", CONST_OVERLOAD(GroundAxiomImpl::get_index), py::return_value_policy::copy)
        .def("get_axiom_index", CONST_OVERLOAD(GroundAxiomImpl::get_axiom_index), py::return_value_policy::copy)
        .def("get_objects", CONST_OVERLOAD(GroundAxiomImpl::get_objects), py::return_value_policy::reference_internal)
        .def("get_strips_precondition", CONST_OVERLOAD(GroundAxiomImpl::get_strips_precondition), py::return_value_policy::reference_internal)
        .def("get_derived_effect", CONST_OVERLOAD(GroundAxiomImpl::get_derived_effect), py::return_value_policy::reference_internal)
        .def("is_applicable", &GroundAxiomImpl::is_applicable, py::arg("state_fluent_atoms"), py::arg("state_derived_atoms"), py::arg("static_positive_atoms"))
        .def("is_statically_applicable", &GroundAxiomImpl::is_statically_applicable, py::arg("static_positive_bitset"))
        .def(auto_pickler<GroundAxiomImpl>());
}