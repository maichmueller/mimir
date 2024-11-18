#include "init_declarations.hpp"
#include "opaque_types.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;

void init_atoms(py::module& m)
{
    auto bind_atom = [&]<typename Tag>(const std::string& class_name, Tag)
    {
        class_<AtomImpl<Tag>>(class_name)
            .def("__str__", &AtomImpl<Tag>::str)
            .def("__repr__", &AtomImpl<Tag>::str)
            .def("get_index", &AtomImpl<Tag>::get_index)
            .def("get_predicate", &AtomImpl<Tag>::get_predicate, py::return_value_policy::reference_internal)
            .def("get_terms", [](const AtomImpl<Tag>& atom) { return atom.get_terms(); }, py::keep_alive<0, 1>());

        static_assert(!py::detail::vector_needs_copy<AtomList<Tag>>::value);
        auto list_cls = py::bind_vector<AtomList<Tag>>(m, class_name + "List");
        def_opaque_vector_repr<AtomList<Tag>>(list_cls, class_name + "List");
    };
    bind_atom("StaticAtom", Static {});
    bind_atom("FluentAtom", Fluent {});
    bind_atom("DerivedAtom", Derived {});

}