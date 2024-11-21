
#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace pymimir;

void init_termvariant(py::module& m)
{
    class_<TermVariant>(m, "Term")  //
        .def("get", [](const TermVariant& arg) -> py::object { return std::visit(AS_LAMBDA(py::cast), *arg.term); }, py::keep_alive<0, 1>());
}