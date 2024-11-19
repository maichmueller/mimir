#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;
using namespace mimir;

void init_function(py::module& m)
{
    using namespace pymimir;



    class_<FunctionSkeletonImpl>(m, "FunctionSkeleton")  //
        .def("__str__", &FunctionSkeletonImpl::str)
        .def("__repr__", &FunctionSkeletonImpl::str)
        .def("get_index", &FunctionSkeletonImpl::get_index)
        .def("get_name", &FunctionSkeletonImpl::get_name, py::return_value_policy::reference_internal)
        .def("get_parameters", [](const FunctionSkeletonImpl& self) { return VariableList(self.get_parameters()); }, py::keep_alive<0, 1>());
    static_assert(!py::detail::vector_needs_copy<FunctionSkeletonList>::value);  // Ensure return by reference + keep alive
    auto list_class = py::bind_vector<FunctionSkeletonList>(m, "FunctionSkeletonList");
    def_opaque_vector_repr<FunctionSkeletonList>(list_class, "FunctionSkeletonList");

    class_<FunctionImpl>(m, "Function")  //
        .def("__str__", &FunctionImpl::str)
        .def("__repr__", &FunctionImpl::str)
        .def("get_index", &FunctionImpl::get_index)
        .def("get_function_skeleton", &FunctionImpl::get_function_skeleton, py::return_value_policy::reference_internal)
        .def("get_terms", [](const FunctionImpl& self) { return to_term_variant_list(self.get_terms()); }, py::keep_alive<0, 1>());
    static_assert(!py::detail::vector_needs_copy<FunctionList>::value);  // Ensure return by reference + keep alive
    list_class = py::bind_vector<FunctionList>(m, "FunctionList");
    def_opaque_vector_repr<FunctionSkeletonList>(list_class, "FunctionList");

    class_<GroundFunctionImpl>(m, "GroundFunction")  //
        .def("__str__", &GroundFunctionImpl::str)
        .def("__repr__", &GroundFunctionImpl::str)
        .def("get_index", &GroundFunctionImpl::get_index)
        .def("get_function_skeleton", &GroundFunctionImpl::get_function_skeleton, py::return_value_policy::reference_internal)
        .def("get_objects", [](const GroundFunctionImpl& self) { return ObjectList(self.get_objects()); }, py::keep_alive<0, 1>());
    static_assert(!py::detail::vector_needs_copy<GroundFunctionList>::value);  // Ensure return by reference + keep alive
    list_class = py::bind_vector<GroundFunctionList>(m, "GroundFunctionList");
    def_opaque_vector_repr<FunctionSkeletonList>(list_class, "GroundFunctionList");
}