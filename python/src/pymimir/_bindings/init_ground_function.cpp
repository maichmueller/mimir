#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

void init_ground_function_expression(py::module& m)
{
    using namespace pymimir;

    class_<GroundFunctionExpressionImpl>(m, "GroundFunctionExpression")  //
        .def("get",
             [](const GroundFunctionExpressionImpl& fexpr) -> py::object
             { return std::visit([](auto&& arg) { return py::cast(arg, py::return_value_policy::reference_internal); }, fexpr.get_variant()); });

    class_<GroundFunctionExpressionNumberImpl>(m, "GroundFunctionExpressionNumber")  //
        .def("__str__", &GroundFunctionExpressionNumberImpl::str)
        .def("__repr__", &GroundFunctionExpressionNumberImpl::str)
        .def("get_index", &GroundFunctionExpressionNumberImpl::get_index)
        .def("get_number", &GroundFunctionExpressionNumberImpl::get_number);

    class_<GroundFunctionExpressionBinaryOperatorImpl>(m, "GroundFunctionExpressionBinaryOperator")  //
        .def("__str__", &GroundFunctionExpressionBinaryOperatorImpl::str)
        .def("__repr__", &GroundFunctionExpressionBinaryOperatorImpl::str)
        .def("get_index", &GroundFunctionExpressionBinaryOperatorImpl::get_index)
        .def("get_binary_operator", &GroundFunctionExpressionBinaryOperatorImpl::get_binary_operator)
        .def("get_left_function_expression",
             &GroundFunctionExpressionBinaryOperatorImpl::get_left_function_expression,
             py::return_value_policy::reference_internal)
        .def("get_right_function_expression",
             &GroundFunctionExpressionBinaryOperatorImpl::get_right_function_expression,
             py::return_value_policy::reference_internal);

    class_<GroundFunctionExpressionMultiOperatorImpl>(m, "GroundFunctionExpressionMultiOperator")  //
        .def("__str__", &GroundFunctionExpressionMultiOperatorImpl::str)
        .def("__repr__", &GroundFunctionExpressionMultiOperatorImpl::str)
        .def("get_index", &GroundFunctionExpressionMultiOperatorImpl::get_index)
        .def("get_multi_operator", &GroundFunctionExpressionMultiOperatorImpl::get_multi_operator)
        .def("get_function_expressions", &GroundFunctionExpressionMultiOperatorImpl::get_function_expressions, py::return_value_policy::reference_internal);

    class_<GroundFunctionExpressionMinusImpl>(m, "GroundFunctionExpressionMinus")  //
        .def("__str__", &GroundFunctionExpressionMinusImpl::str)
        .def("__repr__", &GroundFunctionExpressionMinusImpl::str)
        .def("get_index", &GroundFunctionExpressionMinusImpl::get_index)
        .def("get_function_expression", &GroundFunctionExpressionMinusImpl::get_function_expression, py::return_value_policy::reference_internal);

    class_<GroundFunctionExpressionFunctionImpl>(m, "GroundFunctionExpressionFunction")  //
        .def("__str__", &GroundFunctionExpressionFunctionImpl::str)
        .def("__repr__", &GroundFunctionExpressionFunctionImpl::str)
        .def("get_index", &GroundFunctionExpressionFunctionImpl::get_index)
        .def("get_function", &GroundFunctionExpressionFunctionImpl::get_function, py::return_value_policy::reference_internal);
}