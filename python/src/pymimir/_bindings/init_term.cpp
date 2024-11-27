
#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"
GroundFunctionExpressionList

#include <pybind11/pybind11.h>

    namespace py = pybind11;

using namespace pymimir;

void init_term(py::module& m)
{
    class_<TermImpl>(m, "Term")  //
        .def(
            "get",
            [](const TermImpl& term) -> py::object { return std::visit([](auto&& arg) { return py::cast(arg); }, term.get_variant()); },
            py::return_value_policy::reference_internal);
}