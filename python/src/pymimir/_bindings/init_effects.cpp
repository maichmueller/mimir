#include "init_declarations.hpp"
#include "opaque_types.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;

void init_effects(py::module& m)
{

    class_<EffectSimpleImpl>(m, "EffectSimple")  //
        .def("__str__", &EffectSimpleImpl::str)
        .def("__repr__", &EffectSimpleImpl::str)
        .def("get_index", &EffectSimpleImpl::get_index)
        .def("get_effect", &EffectSimpleImpl::get_effect, py::return_value_policy::reference_internal);
    static_assert(!py::detail::vector_needs_copy<EffectSimpleList>::value);  // Ensure return by reference + keep alive
    auto list_class = py::bind_vector<EffectSimpleList>(m, "EffectSimpleList");
    def_opaque_vector_repr<EffectSimpleList>(list_class, "EffectSimpleList");

    class_<EffectComplexImpl>(m, "EffectComplex")  //
        .def("__str__", &EffectComplexImpl::str)
        .def("__repr__", &EffectComplexImpl::str)
        .def("get_index", &EffectComplexImpl::get_index)
        .def(
            "get_parameters",
            [](const EffectComplexImpl& self) { return VariableList(self.get_parameters()); },
            py::keep_alive<0, 1>())
        .def(
            "get_static_conditions",
            [](const EffectComplexImpl& self) { return LiteralList<Static>(self.get_conditions<Static>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_fluent_conditions",
            [](const EffectComplexImpl& self) { return LiteralList<Fluent>(self.get_conditions<Fluent>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_derived_conditions",
            [](const EffectComplexImpl& self) { return LiteralList<Derived>(self.get_conditions<Derived>()); },
            py::keep_alive<0, 1>())
        .def("get_effect", &EffectComplexImpl::get_effect, py::return_value_policy::reference_internal);
    static_assert(!py::detail::vector_needs_copy<EffectComplexList>::value);  // Ensure return by reference + keep alive
    list_class = py::bind_vector<EffectComplexList>(m, "EffectComplexList");
    def_opaque_vector_repr<EffectComplexList>(list_class, "EffectComplexList");

    class_<StripsActionEffect>(m, "StripsActionEffect")
        .def("get_negative_effects",
             [](const StripsActionEffect& self, const PDDLFactories& factories)
             { return factories.get_ground_atoms_from_indices<mimir::Fluent>(self.get_negative_effects()); })
        .def("get_positive_effects",
             [](const StripsActionEffect& self, const PDDLFactories& factories)
             { return factories.get_ground_atoms_from_indices<mimir::Fluent>(self.get_positive_effects()); })
        .def("get_negative_effect_indices", CONST_OVERLOAD(StripsActionEffect::get_negative_effects), py::return_value_policy::copy)
        .def("get_negative_effect_indices", CONST_OVERLOAD(StripsActionEffect::get_positive_effects), py::return_value_policy::copy);

    class_<SimpleFluentEffect>(m, "SimpleFluentEffect")
        .def_readonly("is_negated", &SimpleFluentEffect::is_negated)
        .def_readonly("atom_index", &SimpleFluentEffect::atom_index);
}