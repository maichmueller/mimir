
#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"
GroundFunctionExpressionList

#include <pybind11/pybind11.h>

    namespace py = pybind11;

using namespace pymimir;

void init_termvariable(py::module& m)
{
    class_<TermVariableImpl>(m, "TermVariable")  //
        .def("__str__", &TermVariableImpl::str)
        .def("__repr__", &TermVariableImpl::str)
        .def("get_index", &TermVariableImpl::get_index)
        .def("get_variable", &TermVariableImpl::get_variable, py::return_value_policy::reference_internal);
}