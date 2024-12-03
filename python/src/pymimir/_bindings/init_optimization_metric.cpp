#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

void init_optimization_metric(py::module& m)
{
    using namespace pymimir;

    // dependent on ground function expression bindings!
    class_<OptimizationMetricImpl>(m, "OptimizationMetric")  //
        .def("__str__", &OptimizationMetricImpl::str)
        .def("__repr__", &OptimizationMetricImpl::str)
        .def("get_index", &OptimizationMetricImpl::get_index)
        .def("get_function_expression", &OptimizationMetricImpl::get_function_expression, py::return_value_policy::reference_internal)
        .def("get_optimization_metric", &OptimizationMetricImpl::get_optimization_metric, py::return_value_policy::reference_internal);
}