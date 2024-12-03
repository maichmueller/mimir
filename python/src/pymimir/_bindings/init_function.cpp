#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

void init_function(py::module& m)
{
    using namespace pymimir;

    class_<FunctionSkeletonImpl>(m, "FunctionSkeleton")  //
        .def("__str__", &FunctionSkeletonImpl::str)
        .def("__repr__", &FunctionSkeletonImpl::str)
        .def("get_index", &FunctionSkeletonImpl::get_index)
        .def("get_name", &FunctionSkeletonImpl::get_name, py::return_value_policy::reference_internal)
        .def("get_parameters", &FunctionSkeletonImpl::get_parameters, py::keep_alive<0, 1>(), py::return_value_policy::copy);

    class_<FunctionImpl>(m, "Function")  //
        .def("__str__", &FunctionImpl::str)
        .def("__repr__", &FunctionImpl::str)
        .def("get_index", &FunctionImpl::get_index)
        .def("get_function_skeleton", &FunctionImpl::get_function_skeleton, py::return_value_policy::reference_internal)
        .def("get_terms", &FunctionImpl::get_terms, py::keep_alive<0, 1>());

    class_<GroundFunctionImpl>(m, "GroundFunction")  //
        .def("__str__", &GroundFunctionImpl::str)
        .def("__repr__", &GroundFunctionImpl::str)
        .def("get_index", &GroundFunctionImpl::get_index)
        .def("get_function_skeleton", &GroundFunctionImpl::get_function_skeleton, py::return_value_policy::reference_internal)
        .def("get_objects", &GroundFunctionImpl::get_objects, py::keep_alive<0, 1>(), py::return_value_policy::copy);
}