#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace pymimir;

void init_function_expression(py::module& m)
{
    class_<FunctionExpressionNumberImpl>(m, "FunctionExpressionNumber")  //
        .def("__str__", [](const FunctionExpressionNumberImpl& self) { return fmt::format("{}", self); })
        .def("__str__", [](const FunctionExpressionNumberImpl& self) { return fmt::format("{}", self); })
        .def("get_index", &FunctionExpressionNumberImpl::get_index)
        .def("get_number", &FunctionExpressionNumberImpl::get_number);

    class_<FunctionExpressionBinaryOperatorImpl>(m, "FunctionExpressionBinaryOperator")  //
        .def("__str__", [](const FunctionExpressionBinaryOperatorImpl& self) { return fmt::format("{}", self); })
        .def("__str__", [](const FunctionExpressionBinaryOperatorImpl& self) { return fmt::format("{}", self); })
        .def("get_index", &FunctionExpressionBinaryOperatorImpl::get_index)
        .def("get_binary_operator", &FunctionExpressionBinaryOperatorImpl::get_binary_operator)
        .def("get_left_function_expression", &FunctionExpressionBinaryOperatorImpl::get_left_function_expression, py::return_value_policy::reference_internal)
        .def("get_right_function_expression",
             &FunctionExpressionBinaryOperatorImpl::get_right_function_expression,
             py::return_value_policy::reference_internal);

    class_<FunctionExpressionMultiOperatorImpl>(m, "FunctionExpressionMultiOperator")  //
        .def("__str__", [](const FunctionExpressionMultiOperatorImpl& self) { return fmt::format("{}", self); })
        .def("__str__", [](const FunctionExpressionMultiOperatorImpl& self) { return fmt::format("{}", self); })
        .def("get_index", &FunctionExpressionMultiOperatorImpl::get_index)
        .def("get_multi_operator", &FunctionExpressionMultiOperatorImpl::get_multi_operator)
        .def("get_function_expressions", &FunctionExpressionMultiOperatorImpl::get_function_expressions, py::return_value_policy::reference_internal);

    class_<FunctionExpressionMinusImpl>(m, "FunctionExpressionMinus")  //
        .def("__str__", [](const FunctionExpressionMinusImpl& self) { return fmt::format("{}", self); })
        .def("__str__", [](const FunctionExpressionMinusImpl& self) { return fmt::format("{}", self); })
        .def("get_index", &FunctionExpressionMinusImpl::get_index)
        .def("get_function_expression", &FunctionExpressionMinusImpl::get_function_expression, py::return_value_policy::reference_internal);

    class_<FunctionExpressionFunctionImpl>(m, "FunctionExpressionFunction")  //
        .def("__str__", [](const FunctionExpressionFunctionImpl& self) { return fmt::format("{}", self); })
        .def("__str__", [](const FunctionExpressionFunctionImpl& self) { return fmt::format("{}", self); })
        .def("get_index", &FunctionExpressionFunctionImpl::get_index)
        .def("get_function", &FunctionExpressionFunctionImpl::get_function, py::return_value_policy::reference_internal);

    class_<FunctionExpressionImpl>(m, "FunctionExpression")  //
        .def(
            "get",
            [](const FunctionExpressionImpl& fexpr) -> py::object { return std::visit([](auto&& arg) { return py::cast(arg); }, fexpr.get_variant()); },
            py::keep_alive<0, 1>());
}