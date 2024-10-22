
#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;


void init_variable(py::module& m)
{
    py::class_<VariableImpl>(m, "Variable")  //
        .def("__str__", &VariableImpl::str)
        .def("__repr__", &VariableImpl::str)
        .def("get_index", &VariableImpl::get_index)
        .def("get_name", &VariableImpl::get_name, py::return_value_policy::reference_internal);
    static_assert(!py::detail::vector_needs_copy<VariableList>::value);  // Ensure return by reference + keep alive
    auto list_class = py::bind_vector<VariableList>(m, "VariableList");
    def_opaque_vector_repr<VariableList>(list_class, "VariableList");
}