#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"
GroundFunctionExpressionList

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
        .def("get_parameters", [](const FunctionSkeletonImpl& self) { return VariableList(self.get_parameters()); }, py::keep_alive<0, 1>());

    class_<FunctionImpl>(m, "Function")  //
        .def("__str__", &FunctionImpl::str)
        .def("__repr__", &FunctionImpl::str)
        .def("get_index", &FunctionImpl::get_index)
        .def("get_function_skeleton", &FunctionImpl::get_function_skeleton, py::return_value_policy::reference_internal)
        .def("get_terms", [](const FunctionImpl& self) { return to_term_variant_list(self.get_terms()); }, py::keep_alive<0, 1>());

    class_<GroundFunctionImpl>(m, "GroundFunction")  //
        .def("__str__", &GroundFunctionImpl::str)
        .def("__repr__", &GroundFunctionImpl::str)
        .def("get_index", &GroundFunctionImpl::get_index)
        .def("get_function_skeleton", &GroundFunctionImpl::get_function_skeleton, py::return_value_policy::reference_internal)
        .def("get_objects", [](const GroundFunctionImpl& self) { return ObjectList(self.get_objects()); }, py::keep_alive<0, 1>());
}