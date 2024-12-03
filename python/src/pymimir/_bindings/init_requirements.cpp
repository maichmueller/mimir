
#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace pymimir;

void init_requirements(py::module& m)
{
    class_<RequirementsImpl>(m, "Requirements")  //
        .def("__str__", &RequirementsImpl::str)
        .def("__repr__", &RequirementsImpl::str)
        .def("get_index", &RequirementsImpl::get_index)
        .def("get_requirements", &RequirementsImpl::get_requirements, py::return_value_policy::reference_internal);
}