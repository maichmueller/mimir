#include "init_declarations.hpp"
#include "opaque_types.hpp"
#include "pymimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

void init_domain(py::module& m)
{
    using namespace pymimir;

    class_<DomainImpl>(m, "Domain")  //
        .def("__str__", [](const DomainImpl& self) { return fmt::format("{}", self); })
        .def("__str__", [](const DomainImpl& self) { return fmt::format("{}", self); })
        .def("get_index", &DomainImpl::get_index)
        .def(
            "get_filepath",
            [](const DomainImpl& self) { return (self.get_filepath().has_value()) ? std::optional<std::string>(self.get_filepath()->string()) : std::nullopt; },
            py::return_value_policy::copy)
        .def("get_name", &DomainImpl::get_name, py::return_value_policy::copy)
        .def("get_constants", &DomainImpl::get_constants, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_static_predicates", &DomainImpl::get_predicates<Static>, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_fluent_predicates", &DomainImpl::get_predicates<Fluent>, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_derived_predicates", &DomainImpl::get_predicates<Derived>, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_functions", &DomainImpl::get_functions, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_actions", &DomainImpl::get_actions, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_predicates",
             [](const py::object& py_domain)
             {
                 const auto& self = py::cast<const DomainImpl&>(py_domain);
                 size_t n_preds = self.get_predicates<Static>().size() + self.get_predicates<Fluent>().size() + self.get_predicates<Derived>().size();
                 py::list all_predicates(n_preds);
                 size_t i = 0;
                 insert_into_list(py_domain, all_predicates, self.get_predicates<Static>(), i);
                 insert_into_list(py_domain, all_predicates, self.get_predicates<Fluent>(), i);
                 insert_into_list(py_domain, all_predicates, self.get_predicates<Derived>(), i);
                 return all_predicates;
             })
        .def("get_requirements", &DomainImpl::get_requirements, py::return_value_policy::reference_internal)
        .def("get_name_to_static_predicate", &DomainImpl::get_name_to_predicate<Static>, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_name_to_fluent_predicate", &DomainImpl::get_name_to_predicate<Fluent>, py::keep_alive<0, 1>(), py::return_value_policy::copy)
        .def("get_name_to_derived_predicate", &DomainImpl::get_name_to_predicate<Derived>, py::keep_alive<0, 1>(), py::return_value_policy::copy);
}