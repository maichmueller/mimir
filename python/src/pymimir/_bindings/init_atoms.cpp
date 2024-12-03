#include "init_declarations.hpp"
#include "mimir/common/itertools.hpp"
#include "opaque_types.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace pymimir;

void init_atoms(py::module& m)
{
    for_each_tag(
        [&]<typename Tag>(Tag, const std::string& class_name = tag_name<Tag>() + "Atom")
        {
            class_<AtomImpl<Tag>>(m, class_name.c_str())
                .def("__str__", &AtomImpl<Tag>::str)
                .def("__repr__", &AtomImpl<Tag>::str)
                .def("get_index", &AtomImpl<Tag>::get_index)
                .def("get_predicate", &AtomImpl<Tag>::get_predicate, py::return_value_policy::reference_internal)
                .def("get_variables", &AtomImpl<Tag>::get_variables, py::return_value_policy::reference_internal)
                .def("get_terms", [](const AtomImpl<Tag>& atom) { return to_term_variant_list(atom.get_terms()); }, py::keep_alive<0, 1> {});
        });
}