
#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace mimir;
using namespace pymimir;

void init_numeric_fluent(py::module& m)
{
    py::class_<NumericFluentImpl>(m, "NumericFluent")  //
        .def("__str__", &NumericFluentImpl::str)
        .def("__repr__", &NumericFluentImpl::str)
        .def("get_index", &NumericFluentImpl::get_index)
        .def("get_function", &NumericFluentImpl::get_function, py::return_value_policy::reference_internal)
        .def("get_number", &NumericFluentImpl::get_number);
    static_assert(!py::detail::vector_needs_copy<NumericFluentList>::value);  // Ensure return by reference + keep alive
    auto list_class = py::bind_vector<NumericFluentList>(m, "NumericFluentList");
    def_opaque_vector_repr<NumericFluentList>(list_class, "NumericFluentList");
}