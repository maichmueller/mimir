#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;
using namespace mimir;

void init_optimization_metric(py::module& m)
{
    using namespace pymimir;


    // dependent on ground function expression bindings!
    class_<OptimizationMetricImpl>("OptimizationMetric")  //
        .def("__str__", &OptimizationMetricImpl::str)
        .def("__repr__", &OptimizationMetricImpl::str)
        .def("get_index", &OptimizationMetricImpl::get_index)
        .def("get_function_expression", [](const OptimizationMetricImpl& metric) { return GroundFunctionExpressionVariant(metric.get_function_expression()); })
        .def("get_optimization_metric", &OptimizationMetricImpl::get_optimization_metric, py::return_value_policy::reference_internal);
}