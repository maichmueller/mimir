
#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace mimir;
using namespace pymimir;

void init_object(py::module& m)
{
    py::class_<ObjectImpl>(m, "Object")  //
        .def("__str__", &ObjectImpl::str)
        .def("__repr__", &ObjectImpl::str)
        .def("get_index", &ObjectImpl::get_index)
        .def("get_name", &ObjectImpl::get_name, py::return_value_policy::reference_internal);
    static_assert(!py::detail::vector_needs_copy<ObjectList>::value);  // Ensure return by reference + keep alive
    auto list_class = py::bind_vector<ObjectList>(m, "ObjectList");
    def_opaque_vector_repr<ObjectList>(list_class, "ObjectList");
}