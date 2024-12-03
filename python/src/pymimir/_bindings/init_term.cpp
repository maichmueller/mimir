
#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace pymimir;

void init_term(py::module& m)
{
    class_<TermImpl>(m, "Term")  //
        .def("get", [](const TermImpl& term) -> py::object { return std::visit(AS_LAMBDA(py::cast), term.get_variant()); }, py::keep_alive<0, 1>());
}