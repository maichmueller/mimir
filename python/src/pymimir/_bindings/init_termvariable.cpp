
#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;


void init_termvariable(py::module& m)
{
    py::class_<TermVariableImpl>(m, "TermVariable")  //
        .def("__str__", &TermVariableImpl::str)
        .def("__repr__", &TermVariableImpl::str)
        .def("get_index", &TermVariableImpl::get_index)
        .def("get_variable", &TermVariableImpl::get_variable, py::return_value_policy::reference_internal);
}