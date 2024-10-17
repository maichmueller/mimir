#include "init_declarations.hpp"
#include "opaque_types.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;

void init_ground_atoms(py::module& m)
{
    auto bind_ground_atom = [&]<typename Tag>(const std::string& class_name, Tag)
    {
        py::class_<GroundAtomImpl<Tag>>(m, class_name.c_str())
            .def("__str__", &GroundAtomImpl<Tag>::str)
            .def("__repr__", &GroundAtomImpl<Tag>::str)
            .def("get_index", &GroundAtomImpl<Tag>::get_index)
            .def("get_arity", &GroundAtomImpl<Tag>::get_arity)
            .def("get_predicate", &GroundAtomImpl<Tag>::get_predicate, py::return_value_policy::reference_internal)
            .def("get_objects", [](const GroundAtomImpl<Tag>& self) { return ObjectList(self.get_objects()); }, py::keep_alive<0, 1>());

        static_assert(!py::detail::vector_needs_copy<GroundAtomList<Tag>>::value);
        auto list_class = py::bind_vector<GroundAtomList<Tag>>(m, class_name + "List")
                              .def(
                                  "lift",
                                  [](const GroundAtomList<Tag>& ground_atoms, PDDLFactories& pddl_factories) { return lift(ground_atoms, pddl_factories); },
                                  py::arg("pddl_factories"));
        def_opaque_vector_repr<GroundAtomList<Tag>>(list_class, class_name + "List");
    };
    bind_ground_atom("StaticGroundAtom", Static {});
    bind_ground_atom("FluentGroundAtom", Fluent {});
    bind_ground_atom("DerivedGroundAtom", Derived {});
}