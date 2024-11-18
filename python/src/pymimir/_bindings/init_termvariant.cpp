
#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;


void init_termvariant(py::module& m)
{
    class_<TermVariant>("Term")  //
        .def("get", [](const TermVariant& arg) -> py::object { return std::visit(AS_LAMBDA(py::cast), *arg.term); }, py::keep_alive<0, 1>());
    static_assert(!py::detail::vector_needs_copy<TermVariantList>::value);  // Ensure return by reference + keep alive
    auto list_class = py::bind_vector<TermVariantList>(m, "TermVariantList");
    def_opaque_vector_repr<TermVariantList>(list_class, "TermVariantList", [](const TermVariant& elem) { return std::visit(repr_visitor, *elem.term); });
}