#include "init_declarations.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;

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
    static_assert(!py::detail::vector_needs_copy<AxiomList>::value);  // Ensure return by reference + keep alive
    auto list_class = py::bind_vector<AxiomList>(m, "AxiomList");
    def_opaque_vector_repr<AxiomList>(list_class, "AxiomList");
}