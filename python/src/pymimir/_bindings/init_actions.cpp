#include "init_declarations.hpp"
#include "opaque_types.hpp"
#include "pymimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace pymimir;

void init_actions(py::module& m)
{
    /* Action */
    class_<ActionImpl>(m, "Action")  //
        .def("__str__", &ActionImpl::str)
        .def("__repr__", &ActionImpl::str)
        .def("get_index", &ActionImpl::get_index)
        .def("get_name", &ActionImpl::get_name, py::return_value_policy::copy)
        .def("get_parameters", &ActionImpl::get_parameters, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_static_conditions", &ActionImpl::get_conditions<Static>, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_fluent_conditions", &ActionImpl::get_conditions<Fluent>, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_derived_conditions", &ActionImpl::get_conditions<Derived>, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_simple_effects", &ActionImpl::get_simple_effects, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_complex_effects", &ActionImpl::get_complex_effects, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_arity", &ActionImpl::get_arity);

    class_<GroundActionImpl>(m, "GroundAction")  //
        .def("__hash__", [](const GroundActionImpl& obj) { return obj.get_index(); })
        .def("__eq__", [](const GroundActionImpl& a, const GroundActionImpl& b) { return a.get_index() == b.get_index(); })
        .def("to_string",
             [](const GroundActionImpl& self, PDDLRepositories& pddl_repositories)
             {
                 std::stringstream ss;
                 ss << std::make_tuple(&self, std::cref(pddl_repositories), FullActionFormatterTag {});
                 return ss.str();
             })
        .def("to_string_for_plan",
             [](const GroundActionImpl& self, PDDLRepositories& pddl_repositories)
             {
                 std::stringstream ss;
                 ss << std::make_tuple(&self, std::cref(pddl_repositories), PlanActionFormatterTag {});
                 return ss.str();
             })
        .def("get_index", CONST_OVERLOAD(GroundActionImpl::get_index))
        .def("get_cost", CONST_OVERLOAD(GroundActionImpl::get_cost))
        .def("get_action_index", CONST_OVERLOAD(GroundActionImpl::get_action_index), py::return_value_policy::reference_internal)
        .def("get_object_indices", &GroundActionImpl::get_object_indices, py::return_value_policy::copy)
        .def("get_strips_precondition", [](const GroundActionImpl& self) { return StripsActionPrecondition(self.get_strips_precondition()); })
        .def("get_strips_effect", [](const GroundActionImpl& self) { return StripsActionEffect(self.get_strips_effect()); })
        .def("get_conditional_effects",
             [](const GroundActionImpl& self)
             { return insert_into_list(make_range(self.get_conditional_effects().begin(), self.get_conditional_effects().end())); });
}