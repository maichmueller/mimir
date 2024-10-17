#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;
using namespace mimir;


void init_ground_function_expression(py::module& m)
{
    using namespace pymimir;

    py::class_<GroundFunctionExpressionVariant>(m, "GroundFunctionExpression")  //
        .def(
            "get",
            [](const GroundFunctionExpressionVariant& arg) -> py::object { return std::visit(cast_visitor, *arg.function_expression); },
            py::keep_alive<0, 1>());

    static_assert(!py::detail::vector_needs_copy<GroundFunctionExpressionVariantList>::value);  // Ensure return by reference + keep alive
    auto list_class = py::bind_vector<GroundFunctionExpressionVariantList>(m, "GroundFunctionExpressionVariantList");
    def_opaque_vector_repr<GroundFunctionExpressionVariantList>(list_class, "GroundFunctionExpressionVariantList");

    py::class_<GroundFunctionExpressionNumberImpl>(m, "GroundFunctionExpressionNumber")  //
        .def("__str__", &GroundFunctionExpressionNumberImpl::str)
        .def("__repr__", &GroundFunctionExpressionNumberImpl::str)
        .def("get_index", &GroundFunctionExpressionNumberImpl::get_index)
        .def("get_number", &GroundFunctionExpressionNumberImpl::get_number);

    py::class_<GroundFunctionExpressionBinaryOperatorImpl>(m, "GroundFunctionExpressionBinaryOperator")  //
        .def("__str__", &GroundFunctionExpressionBinaryOperatorImpl::str)
        .def("__repr__", &GroundFunctionExpressionBinaryOperatorImpl::str)
        .def("get_index", &GroundFunctionExpressionBinaryOperatorImpl::get_index)
        .def("get_binary_operator", &GroundFunctionExpressionBinaryOperatorImpl::get_binary_operator)
        .def(
            "get_left_function_expression",
            [](const GroundFunctionExpressionBinaryOperatorImpl& function_expression)
            { return GroundFunctionExpressionVariant(function_expression.get_left_function_expression()); },
            py::keep_alive<0, 1>())
        .def(
            "get_right_function_expression",
            [](const GroundFunctionExpressionBinaryOperatorImpl& function_expression)
            { return GroundFunctionExpressionVariant(function_expression.get_right_function_expression()); },
            py::keep_alive<0, 1>());

    py::class_<GroundFunctionExpressionMultiOperatorImpl>(m, "GroundFunctionExpressionMultiOperator")  //
        .def("__str__", &GroundFunctionExpressionMultiOperatorImpl::str)
        .def("__repr__", &GroundFunctionExpressionMultiOperatorImpl::str)
        .def("get_index", &GroundFunctionExpressionMultiOperatorImpl::get_index)
        .def("get_multi_operator", &GroundFunctionExpressionMultiOperatorImpl::get_multi_operator)
        .def(
            "get_function_expressions",
            [](const GroundFunctionExpressionMultiOperatorImpl& function_expression)
            { return to_ground_function_expression_variant_list(function_expression.get_function_expressions()); },
            py::keep_alive<0, 1>());

    py::class_<GroundFunctionExpressionMinusImpl>(m, "GroundFunctionExpressionMinus")  //
        .def("__str__", &GroundFunctionExpressionMinusImpl::str)
        .def("__repr__", &GroundFunctionExpressionMinusImpl::str)
        .def("get_index", &GroundFunctionExpressionMinusImpl::get_index)
        .def(
            "get_function_expression",
            [](const GroundFunctionExpressionMinusImpl& function_expression)
            { return GroundFunctionExpressionVariant(function_expression.get_function_expression()); },
            py::keep_alive<0, 1>());

    py::class_<GroundFunctionExpressionFunctionImpl>(m, "GroundFunctionExpressionFunction")  //
        .def("__str__", &GroundFunctionExpressionFunctionImpl::str)
        .def("__repr__", &GroundFunctionExpressionFunctionImpl::str)
        .def("get_index", &GroundFunctionExpressionFunctionImpl::get_index)
         .def("get_function", &GroundFunctionExpressionFunctionImpl::get_function, py::return_value_policy::reference_internal);
}