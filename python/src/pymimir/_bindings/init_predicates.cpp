#include "init_declarations.hpp"
#include "mimir/common/itertools.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace pymimir;

void init_predicates(py::module& m)
{
    for_each_tag(
        [&]<typename Tag>(Tag, std::string class_name = tag_name<Tag>() + "Predicate")
        {
            class_<PredicateImpl<Tag>>(m, class_name.c_str())
                .def("__str__", &PredicateImpl<Tag>::str)
                .def("__repr__", &PredicateImpl<Tag>::str)
                .def("get_index", &PredicateImpl<Tag>::get_index)
                .def("get_name", &PredicateImpl<Tag>::get_name, py::return_value_policy::reference_internal)
                .def(
                    "get_parameters",
                    [](const PredicateImpl<Tag>& self) { return VariableList(self.get_parameters()); },
                    py::keep_alive<0, 1>())
                .def("get_arity", &PredicateImpl<Tag>::get_arity);
        });
}