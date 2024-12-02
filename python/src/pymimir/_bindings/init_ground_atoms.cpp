#include "init_declarations.hpp"
#include "mimir/common/itertools.hpp"
#include "opaque_types.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace pymimir;

void init_ground_atoms(py::module& m)
{
    for_each_tag(
        [&]<typename Tag>(Tag, const std::string& class_name = tag_name<Tag>() + "GroundAtom")
        {
            class_<GroundAtomImpl<Tag>>(m, class_name.c_str())
                .def("__str__", &GroundAtomImpl<Tag>::str)
                .def("__repr__", &GroundAtomImpl<Tag>::str)
                .def("get_index", &GroundAtomImpl<Tag>::get_index)
                .def("get_arity", &GroundAtomImpl<Tag>::get_arity)
                .def("get_predicate", &GroundAtomImpl<Tag>::get_predicate, py::return_value_policy::reference_internal)
                .def("get_objects", [](const GroundAtomImpl<Tag>& self) { return ObjectList(self.get_objects()); }, py::keep_alive<0, 1>());
        });
}