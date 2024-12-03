
#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace pymimir;

void init_variable(py::module& m)
{
    class_<VariableImpl>(m, "Variable")  //
        .def("__str__", [](const VariableImpl& self) { return fmt::format("{}", self); })
        .def("__str__", [](const VariableImpl& self) { return fmt::format("{}", self); })
        .def("get_index", &VariableImpl::get_index)
        .def("get_name", &VariableImpl::get_name, py::return_value_policy::reference_internal);
}