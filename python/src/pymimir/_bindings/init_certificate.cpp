#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "opaque_types.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;
using namespace pymimir;

void init_certificate(py::module& m)
{
    // Certificate
    py::class_<Certificate, std::shared_ptr<Certificate>>(m, "Certificate")
        .def(py::init<size_t, size_t, std::string, ColorList>(),
             py::arg("num_vertices"),
             py::arg("num_edges"),
             py::arg("nauty_certificate"),
             py::arg("canonical_initial_coloring"))
        .def("__eq__", &Certificate::operator==)
        .def("__hash__", [](const Certificate& self) { return std::hash<Certificate>()(self); })
        .def("get_num_vertices", &Certificate::get_num_vertices)
        .def("get_num_edges", &Certificate::get_num_edges)
        .def("get_nauty_certificate", &Certificate::get_nauty_certificate, py::return_value_policy::reference_internal)
        .def("get_canonical_initial_coloring", &Certificate::get_canonical_initial_coloring, py::return_value_policy::reference_internal);

}