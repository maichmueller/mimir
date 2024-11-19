
#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;


void init_variable(py::module& m)
{
    class_<VariableImpl>(m, "Variable")  //
        .def("__str__", &VariableImpl::str)
        .def("__repr__", &VariableImpl::str)
        .def("get_index", &VariableImpl::get_index)
        .def("get_name", &VariableImpl::get_name, py::return_value_policy::reference_internal);

}