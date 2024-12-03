
#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace pymimir;

void init_object(py::module& m)
{
    class_<ObjectImpl>(m, "Object")  //
        .def("__str__", [](const ObjectImpl& self) { return fmt::format("{}", self); })
        .def("__str__", [](const ObjectImpl& self) { return fmt::format("{}", self); })
        .def("get_index", &ObjectImpl::get_index)
        .def("get_name", &ObjectImpl::get_name, py::return_value_policy::reference_internal);
}