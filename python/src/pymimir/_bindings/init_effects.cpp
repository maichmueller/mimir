#include "init_declarations.hpp"
#include "mimir/common/itertools.hpp"
#include "mimir/common/printers.hpp"
#include "opaque_types.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace pymimir;

void init_effects(py::module& m)
{
    class_<EffectStripsImpl>(m, "EffectStrips")  //
        .def("__str__", [](const EffectStripsImpl& self) { return fmt::format("{}", self); })
        .def("__str__", [](const EffectStripsImpl& self) { return fmt::format("{}", self); })
        .def("get_index", &EffectStripsImpl::get_index)
        .def("get_effects", &EffectStripsImpl::get_effects, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_function_expression", &EffectStripsImpl::get_function_expression, py::return_value_policy::reference_internal);

    class_<EffectConditionalImpl>(m, "EffectConditional")  //
        .def("__str__", [](const EffectConditionalImpl& self) { return fmt::format("{}", self); })
        .def("__str__", [](const EffectConditionalImpl& self) { return fmt::format("{}", self); })
        .def("get_index", &EffectConditionalImpl::get_index)
        .def(
            "get_parameters",
            [](const EffectConditionalImpl& self) { return VariableList(self.get_parameters()); },
            py::keep_alive<0, 1>())
        .def(
            "get_static_conditions",
            [](const EffectConditionalImpl& self) { return LiteralList<Static>(self.get_conditions<Static>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_fluent_conditions",
            [](const EffectConditionalImpl& self) { return LiteralList<Fluent>(self.get_conditions<Fluent>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_derived_conditions",
            [](const EffectConditionalImpl& self) { return LiteralList<Derived>(self.get_conditions<Derived>()); },
            py::keep_alive<0, 1>())
        .def("get_effects", &EffectConditionalImpl::get_effects, py::return_value_policy::reference_internal);

    class_<GroundEffectStrips>(m, "GroundEffectStrips")
        .def("get_negative_effects",
             [](const GroundEffectStrips& self, const PDDLRepositories& factories)
             { return factories.get_ground_atoms_from_indices<mimir::Fluent>(self.get_negative_effects()); })
        .def("get_positive_effects",
             [](const GroundEffectStrips& self, const PDDLRepositories& factories)
             { return factories.get_ground_atoms_from_indices<mimir::Fluent>(self.get_positive_effects()); })
        .def("get_negative_effect_indices", CONST_OVERLOAD(GroundEffectStrips::get_negative_effects), py::return_value_policy::copy)
        .def("get_negative_effect_indices", CONST_OVERLOAD(GroundEffectStrips::get_positive_effects), py::return_value_policy::copy)
        .def("get_cost", CONST_OVERLOAD(GroundEffectStrips::get_cost), py::return_value_policy::copy);

    for_each_tag(
        [&]<typename Tag>(Tag, std::string tag = tag_name<Tag>())
        {
            const std::string class_name = "GroundEffect" + tag + "Literal";
            class_<GroundEffectLiteral<Tag>>(m, class_name.c_str())
                .def_readonly("is_negated", &GroundEffectLiteral<Tag>::is_negated)
                .def_readonly("atom_index", &GroundEffectLiteral<Tag>::atom_index);
        });
}