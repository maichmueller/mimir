
#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;


void init_numeric_fluent(py::module& m)
{
    class_<NumericFluentImpl>(m, "NumericFluent")  //
        .def("__str__", &NumericFluentImpl::str)
        .def("__repr__", &NumericFluentImpl::str)
        .def("get_index", &NumericFluentImpl::get_index)
        .def("get_function", &NumericFluentImpl::get_function, py::return_value_policy::reference_internal)
        .def("get_number", &NumericFluentImpl::get_number);
}