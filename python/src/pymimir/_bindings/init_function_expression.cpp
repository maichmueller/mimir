#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"
GroundFunctionExpressionList

#include <pybind11/pybind11.h>

    namespace py = pybind11;

using namespace pymimir;

void init_function_expression(py::module& m)
{
    class_<FunctionExpressionNumberImpl>(m, "FunctionExpressionNumber")  //
        .def("__str__", &FunctionExpressionNumberImpl::str)
        .def("__repr__", &FunctionExpressionNumberImpl::str)
        .def("get_index", &FunctionExpressionNumberImpl::get_index)
        .def("get_number", &FunctionExpressionNumberImpl::get_number);

    class_<FunctionExpressionBinaryOperatorImpl>(m, "FunctionExpressionBinaryOperator")  //
        .def("__str__", &FunctionExpressionBinaryOperatorImpl::str)
        .def("__repr__", &FunctionExpressionBinaryOperatorImpl::str)
        .def("get_index", &FunctionExpressionBinaryOperatorImpl::get_index)
        .def("get_binary_operator", &FunctionExpressionBinaryOperatorImpl::get_binary_operator)
        .def("get_left_function_expression",
             &GroundFunctionExpressionBinaryOperatorImpl::get_left_function_expression,
             py::return_value_policy::reference_internal)
        .def("get_right_function_expression",
             &GroundFunctionExpressionBinaryOperatorImpl::get_right_function_expression,
             py::return_value_policy::reference_internal);

    class_<FunctionExpressionMultiOperatorImpl>(m, "FunctionExpressionMultiOperator")  //
        .def("__str__", &FunctionExpressionMultiOperatorImpl::str)
        .def("__repr__", &FunctionExpressionMultiOperatorImpl::str)
        .def("get_index", &FunctionExpressionMultiOperatorImpl::get_index)
        .def("get_multi_operator", &FunctionExpressionMultiOperatorImpl::get_multi_operator)
        .def("get_function_expressions",
             &GroundFunctionExpressionMultiOperatorImpl::get_function_expressions,
             py::keep_alive<0, 1>(),
             py::return_value_policy::copy);

    class_<FunctionExpressionMinusImpl>(m, "FunctionExpressionMinus")  //
        .def("__str__", &FunctionExpressionMinusImpl::str)
        .def("__repr__", &FunctionExpressionMinusImpl::str)
        .def("get_index", &FunctionExpressionMinusImpl::get_index)
        .def("get_function_expression", &FunctionExpressionMinusImpl::get_function_expression, py::return_value_policy::reference_internal);

    class_<FunctionExpressionFunctionImpl>(m, "FunctionExpressionFunction")  //
        .def("__str__", &FunctionExpressionFunctionImpl::str)
        .def("__repr__", &FunctionExpressionFunctionImpl::str)
        .def("get_index", &FunctionExpressionFunctionImpl::get_index)
        .def("get_function", &FunctionExpressionFunctionImpl::get_function, py::return_value_policy::reference_internal);

    class_<FunctionExpressionImpl>(m, "FunctionExpression")  //
        .def(
            "get",
            [](const FunctionExpressionImpl& fexpr) -> py::object { return std::visit([](auto&& arg) { return py::cast(arg); }, fexpr.get_variant()); },
            py::return_value_policy::reference_internal);
}