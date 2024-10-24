#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "opaque_types.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;

void init_actions(py::module& m)
{
    /* Action */
    py::class_<ActionImpl>(m, "Action")  //
        .def("__str__", &ActionImpl::str)
        .def("__repr__", &ActionImpl::str)
        .def("get_index", &ActionImpl::get_index)
        .def("get_name", &ActionImpl::get_name, py::return_value_policy::copy)
        .def(
            "get_parameters",
            [](const ActionImpl& self) { return VariableList(self.get_parameters()); },
            py::keep_alive<0, 1>())
        .def(
            "get_static_conditions",
            [](const ActionImpl& self) { return LiteralList<Static>(self.get_conditions<Static>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_fluent_conditions",
            [](const ActionImpl& self) { return LiteralList<Fluent>(self.get_conditions<Fluent>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_derived_conditions",
            [](const ActionImpl& self) { return LiteralList<Derived>(self.get_conditions<Derived>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_simple_effects",
            [](const ActionImpl& self) { return EffectSimpleList(self.get_simple_effects()); },
            py::keep_alive<0, 1>())
        .def(
            "get_complex_effects",
            [](const ActionImpl& self) { return EffectComplexList(self.get_complex_effects()); },
            py::keep_alive<0, 1>())
        .def("get_arity", &ActionImpl::get_arity);
    static_assert(!py::detail::vector_needs_copy<ActionList>::value);  // Ensure return by reference + keep alive
    auto list_class = py::bind_vector<ActionList>(m, "ActionList");
    def_opaque_vector_repr<ActionList>(list_class, "ActionList");

    py::class_<GroundAction>(m, "GroundAction")  //
        .def("__hash__", [](const GroundAction& obj) { return std::hash<GroundAction>()(obj); })
        .def("__eq__", [](const GroundAction& a, const GroundAction& b) { return std::equal_to<GroundAction>()(a, b); })
        .def("to_string",
             [](GroundAction self, PDDLFactories& pddl_factories)
             {
                 std::stringstream ss;
                 ss << std::make_tuple(self, std::cref(pddl_factories));
                 return ss.str();
             })
        .def("to_string_for_plan",
             [](GroundAction self, PDDLFactories& pddl_factories)
             {
                 std::stringstream ss;
                 ss << std::make_tuple(std::cref(pddl_factories), self);
                 return ss.str();
             })
        .def("get_index", &GroundAction::get_index)
        .def("get_cost", &GroundAction::get_cost)
        .def("get_action_index", &GroundAction::get_action_index, py::return_value_policy::reference_internal)
        .def("get_object_indices", &GroundAction::get_object_indices, py::return_value_policy::copy)
        .def("get_strips_precondition", [](const GroundAction& self) { return StripsActionPrecondition(self.get_strips_precondition()); })
        .def("get_strips_effect", [](const GroundAction& self) { return StripsActionEffect(self.get_strips_effect()); })
        .def(
            "get_conditional_effects",
            [](const GroundAction& self) { return ConditionalEffectList(self.get_conditional_effects().begin(), self.get_conditional_effects().end()); },
            py::keep_alive<0, 1>());
    static_assert(!py::detail::vector_needs_copy<GroundActionList>::value);  // Ensure return by reference + keep alive
    list_class = py::bind_vector<GroundActionList>(m, "GroundActionList");
    def_opaque_vector_repr<GroundActionList>(list_class, "GroundActionList");
    bind_const_span<std::span<const GroundAction>>(m, "GroundActionSpan");
}