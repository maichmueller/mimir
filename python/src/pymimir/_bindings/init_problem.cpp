#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;


void init_problem(py::module& m)
{
    using namespace pymimir;
    class_<ProblemImpl>(m, "Problem")  //
        .def("__str__", &ProblemImpl::str)
        .def("__repr__", &ProblemImpl::str)
        .def("get_index", &ProblemImpl::get_index)
        .def(
            "get_filepath",
            [](const ProblemImpl& self)
            { return (self.get_filepath().has_value()) ? std::optional<std::string>(self.get_filepath()->string()) : std::nullopt; },
            py::return_value_policy::copy)
        .def("get_name", &ProblemImpl::get_name, py::return_value_policy::copy)
        .def("get_domain", &ProblemImpl::get_domain, py::return_value_policy::reference_internal)
        .def("get_requirements", &ProblemImpl::get_requirements, py::return_value_policy::reference_internal)
        .def(
            "get_objects",
            [](const ProblemImpl& self) { return ObjectList(self.get_objects()); },
            py::keep_alive<0, 1>())
        .def(
            "get_static_initial_literals",
            [](const ProblemImpl& self) { return GroundLiteralList<Static>(self.get_static_initial_literals()); },
            py::keep_alive<0, 1>())
        .def(
            "get_fluent_initial_literals",
            [](const ProblemImpl& self) { return GroundLiteralList<Fluent>(self.get_fluent_initial_literals()); },
            py::keep_alive<0, 1>())
        .def(
            "get_numeric_fluents",
            [](const ProblemImpl& self) { return NumericFluentList(self.get_numeric_fluents()); },
            py::keep_alive<0, 1>())
        .def("get_optimization_metric", &ProblemImpl::get_optimization_metric, py::return_value_policy::reference_internal)
        .def(
            "get_static_goal_condition",
            [](const ProblemImpl& self) { return GroundLiteralList<Static>(self.get_goal_condition<Static>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_fluent_goal_condition",
            [](const ProblemImpl& self) { return GroundLiteralList<Fluent>(self.get_goal_condition<Fluent>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_derived_goal_condition",
            [](const ProblemImpl& self) { return GroundLiteralList<Derived>(self.get_goal_condition<Derived>()); },
            py::keep_alive<0, 1>())
        .def("get_goal_condition",
             [](const py::object& py_problem)
             {
                 const auto& self = py::cast<const ProblemImpl&>(py_problem);
                 size_t n_goals =
                     self.get_goal_condition<Static>().size() + self.get_goal_condition<Fluent>().size() + self.get_goal_condition<Derived>().size();
                 py::list all_goal_literals(n_goals);
                 int i = 0;
                 insert_into_list(py_problem, all_goal_literals, self.get_goal_condition<Static>(), i);
                 insert_into_list(py_problem, all_goal_literals, self.get_goal_condition<Fluent>(), i);
                 insert_into_list(py_problem, all_goal_literals, self.get_goal_condition<Derived>(), i);
                 return all_goal_literals;
             });

}