#include "init_declarations.hpp"
#include "opaque_types.hpp"
#include "pymimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace pymimir;

void init_conditional_effect(py::module& m)
{
    class_<GroundEffectConditional>(m, "GroundEffectConditional")
        .def("get_positive_condition", &all_atoms_from_conditions<true, const GroundEffectConditional>)
        .def("get_fluent_positive_condition", &atoms_from_conditions<true, Fluent, GroundEffectConditional>)
        .def("get_static_positive_condition", &atoms_from_conditions<true, Static, GroundEffectConditional>)
        .def("get_derived_positive_condition", &atoms_from_conditions<true, Derived, GroundEffectConditional>)
        .def("get_negative_condition", &all_atoms_from_conditions<false, GroundEffectConditional>)
        .def("get_fluent_positive_condition", &atoms_from_conditions<false, Fluent, GroundEffectConditional>)
        .def("get_static_positive_condition", &atoms_from_conditions<false, Static, GroundEffectConditional>)
        .def("get_derived_positive_condition", &atoms_from_conditions<false, Derived, GroundEffectConditional>)
        .def("get_fluent_effect_literals", CONST_OVERLOAD(GroundEffectConditional::get_fluent_effect_literals));
}