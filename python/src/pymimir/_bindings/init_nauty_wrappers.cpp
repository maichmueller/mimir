#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "opaque_types.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;


void init_nauty_wrappers(py::module& m)
{
    // NautyCertificate
    py::class_<nauty_wrapper::Certificate, std::shared_ptr<nauty_wrapper::Certificate>>(m, "NautyCertificate")
        .def("__eq__", &nauty_wrapper::Certificate::operator==)
        .def("__hash__", [](const nauty_wrapper::Certificate& self) { return std::hash<nauty_wrapper::Certificate>()(self); })
        .def("get_canonical_graph", &nauty_wrapper::Certificate::get_canonical_graph, py::return_value_policy::reference_internal)
        .def("get_canonical_coloring", &nauty_wrapper::Certificate::get_canonical_coloring, py::return_value_policy::reference_internal);

    // NautyDenseGraph
    py::class_<nauty_wrapper::DenseGraph>(m, "NautyDenseGraph")  //
        .def(py::init<>())
        .def(py::init<int>(), py::arg("num_vertices"))
        .def(py::init<StaticVertexColoredDigraph>(), py::arg("digraph"))
        .def("add_edge", &nauty_wrapper::DenseGraph::add_edge, py::arg("source"), py::arg("target"))
        .def("compute_certificate", &nauty_wrapper::DenseGraph::compute_certificate)
        .def("clear", &nauty_wrapper::DenseGraph::clear, py::arg("num_vertices"));

    // NautySparseGraph
    py::class_<nauty_wrapper::SparseGraph>(m, "NautySparseGraph")  //
        .def(py::init<>())
        .def(py::init<int>(), py::arg("num_vertices"))
        .def(py::init<StaticVertexColoredDigraph>(), py::arg("digraph"))
        .def("add_edge", &nauty_wrapper::SparseGraph::add_edge, py::arg("source"), py::arg("target"))
        .def("compute_certificate", &nauty_wrapper::SparseGraph::compute_certificate)
        .def("clear", &nauty_wrapper::SparseGraph::clear, py::arg("num_vertices"));
}