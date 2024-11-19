#include "init_declarations.hpp"
#include "opaque_types.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;

void init_atoms(py::module& m)
{
    auto bind_atom = [&]<typename Tag>(const std::string& class_name, Tag)
    {
        class_<AtomImpl<Tag>>(m, class_name.c_str())
            .def("__str__", &AtomImpl<Tag>::str)
            .def("__repr__", &AtomImpl<Tag>::str)
            .def("get_index", &AtomImpl<Tag>::get_index)
            .def("get_predicate", &AtomImpl<Tag>::get_predicate, py::return_value_policy::reference_internal)
            .def("get_terms", [](const AtomImpl<Tag>& atom) { return atom.get_terms(); }, py::keep_alive<0, 1>());
    };
    bind_atom("StaticAtom", Static {});
    bind_atom("FluentAtom", Fluent {});
    bind_atom("DerivedAtom", Derived {});

}