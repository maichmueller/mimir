#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "opaque_types.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

void init_domain(py::module& m)
{
    using namespace mimir;
using namespace mimir::pymimir;

    class_<DomainImpl>("Domain")  //
        .def("__str__", &DomainImpl::str)
        .def("__repr__", &DomainImpl::str)
        .def("get_index", &DomainImpl::get_index)
        .def(
            "get_filepath",
            [](const DomainImpl& self) { return (self.get_filepath().has_value()) ? std::optional<std::string>(self.get_filepath()->string()) : std::nullopt; },
            py::return_value_policy::copy)
        .def("get_name", &DomainImpl::get_name, py::return_value_policy::copy)
        .def(
            "get_constants",
            [](const DomainImpl& self) { return ObjectList(self.get_constants()); },
            py::keep_alive<0, 1>())
        .def(
            "get_static_predicates",
            [](const DomainImpl& self) { return PredicateList<Static>(self.get_predicates<Static>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_fluent_predicates",
            [](const DomainImpl& self) { return PredicateList<Fluent>(self.get_predicates<Fluent>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_derived_predicates",
            [](const DomainImpl& self) { return PredicateList<Derived>(self.get_predicates<Derived>()); },
            py::keep_alive<0, 1>())
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
        .def(
            "get_functions",
            [](const DomainImpl& self) { return FunctionSkeletonList(self.get_functions()); },
            py::keep_alive<0, 1>())
        .def(
            "get_actions",
            [](const DomainImpl& self) { return ActionList(self.get_actions()); },
            py::keep_alive<0, 1>())
        .def("get_requirements", &DomainImpl::get_requirements, py::return_value_policy::reference_internal)
        .def(
            "get_name_to_static_predicate",
            [](const DomainImpl& self) { return ToPredicateMap<std::string, Static>(self.get_name_to_predicate<Static>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_name_to_fluent_predicate",
            [](const DomainImpl& self) { return ToPredicateMap<std::string, Fluent>(self.get_name_to_predicate<Fluent>()); },
            py::keep_alive<0, 1>())
        .def(
            "get_name_to_derived_predicate",
            [](const DomainImpl& self) { return ToPredicateMap<std::string, Derived>(self.get_name_to_predicate<Derived>()); },
            py::keep_alive<0, 1>());
    static_assert(!py::detail::vector_needs_copy<DomainList>::value);  // Ensure return by reference + keep alive
    auto list_class = py::bind_vector<DomainList>(m, "DomainList");
    def_opaque_vector_repr<DomainList>(list_class, "DomainList");
}