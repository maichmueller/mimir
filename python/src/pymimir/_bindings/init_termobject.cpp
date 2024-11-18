
#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;


void init_termobject(py::module& m)
{
    class_<TermObjectImpl>("TermObject")  //
        .def("__str__", &TermObjectImpl::str)
        .def("__repr__", &TermObjectImpl::str)
        .def("get_index", &TermObjectImpl::get_index)
        .def("get_object", &TermObjectImpl::get_object, py::return_value_policy::reference_internal);
}