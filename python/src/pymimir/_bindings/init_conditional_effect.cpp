#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "opaque_types.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;


void init_conditional_effect(py::module& m)
{
    class_<ConditionalEffect>(m, "ConditionalEffect")
        .def("get_positive_condition", &all_atoms_from_conditions<true, const ConditionalEffect>)
        .def("get_fluent_positive_condition", &atoms_from_conditions<true, Fluent, ConditionalEffect>)
        .def("get_static_positive_condition", &atoms_from_conditions<true, Static, ConditionalEffect>)
        .def("get_derived_positive_condition", &atoms_from_conditions<true, Derived, ConditionalEffect>)
        .def("get_negative_condition", &all_atoms_from_conditions<false, ConditionalEffect>)
        .def("get_fluent_positive_condition", &atoms_from_conditions<false, Fluent, ConditionalEffect>)
        .def("get_static_positive_condition", &atoms_from_conditions<false, Static, ConditionalEffect>)
        .def("get_derived_positive_condition", &atoms_from_conditions<false, Derived, ConditionalEffect>)
        .def("get_simple_effect", CONST_OVERLOAD(ConditionalEffect::get_simple_effect));
}