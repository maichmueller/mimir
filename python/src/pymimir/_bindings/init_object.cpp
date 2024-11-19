
#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;


void init_object(py::module& m)
{

    class_<ObjectImpl>(m, "Object")  //
        .def("__str__", &ObjectImpl::str)
        .def("__repr__", &ObjectImpl::str)
        .def("get_index", &ObjectImpl::get_index)
        .def("get_name", &ObjectImpl::get_name, py::return_value_policy::reference_internal);

}