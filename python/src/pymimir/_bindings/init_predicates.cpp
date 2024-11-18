#include "init_declarations.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;

void init_predicates(py::module& m)
{
    auto bind_predicate = [&]<typename Tag>(const std::string& class_name, Tag)
    {
        class_<PredicateImpl<Tag>>(class_name)
            .def("__str__", &PredicateImpl<Tag>::str)
            .def("__repr__", &PredicateImpl<Tag>::str)
            .def("get_index", &PredicateImpl<Tag>::get_index)
            .def("get_name", &PredicateImpl<Tag>::get_name, py::return_value_policy::reference_internal)
            .def(
                "get_parameters",
                [](const PredicateImpl<Tag>& self) { return VariableList(self.get_parameters()); },
                py::keep_alive<0, 1>())
            .def("get_arity", &PredicateImpl<Tag>::get_arity);

        static_assert(!py::detail::vector_needs_copy<PredicateList<Tag>>::value);
        auto predicate_list = py::bind_vector<PredicateList<Tag>>(m, class_name + "List");
        def_opaque_vector_repr<PredicateList<Tag>>(predicate_list, class_name + "List");
        py::bind_map<ToPredicateMap<std::string, Tag>>(m, "StringTo" + class_name + "Map");
    };
    bind_predicate("StaticPredicate", Static {});
    bind_predicate("FluentPredicate", Fluent {});
    bind_predicate("DerivedPredicate", Derived {});
}