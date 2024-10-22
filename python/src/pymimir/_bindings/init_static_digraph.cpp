#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;
using namespace mimir;
using namespace mimir::pymimir;


void init_static_digraph(py::module& m)
{

     // EmptyVertex (used in StaticDigraph)
     py::class_<EmptyVertex>(m, "EmptyVertex")
         .def("__eq__", &EmptyVertex::operator==)
         .def("__hash__", [](const EmptyVertex& self) { return std::hash<EmptyVertex>()(self); })
         .def_property_readonly("index", &EmptyVertex::get_index);

     // EmptyEdge (used in StaticDigraph)
     py::class_<EmptyEdge>(m, "EmptyEdge")
         .def("__eq__", &EmptyEdge::operator==)
         .def("__hash__", [](const EmptyEdge& self) { return std::hash<EmptyEdge>()(self); })
         .def_property_readonly("index", &EmptyEdge::get_index)
         .def_property_readonly("source", &EmptyEdge::get_source)
         .def_property_readonly("target", &EmptyEdge::get_target);

     // StaticDigraph
     py::class_<StaticDigraph>(m, "StaticDigraph")  //
         .def(py::init<>())
         .def("__str__",
              [](const StaticDigraph& self)
              {
                  std::stringstream ss;
                  ss << self;
                  return ss.str();
              })
         .def("add_vertex", [](StaticDigraph& self) -> VertexIndex { return self.add_vertex(); })
         .def("add_directed_edge",
              [](StaticDigraph& self, VertexIndex source, VertexIndex target) -> EdgeIndex { return self.add_directed_edge(source, target); })
         .def("add_undirected_edge",
              [](StaticDigraph& self, VertexIndex source, VertexIndex target) -> std::pair<EdgeIndex, EdgeIndex>
              { return self.add_undirected_edge(source, target); })
         .def("clear", &StaticDigraph::clear)
         .def(
             "get_forward_adjacent_vertices",
             [](const StaticDigraph& self, VertexIndex vertex)
             {
                 auto iterator = self.get_adjacent_vertices<ForwardTraversal>(vertex);
                 return py::make_iterator(iterator.begin(), iterator.end());
             },
             py::keep_alive<0, 1>(),
             py::arg("vertex_index"))
         .def(
             "get_backward_adjacent_vertices",
             [](const StaticDigraph& self, VertexIndex vertex)
             {
                 auto iterator = self.get_adjacent_vertices<BackwardTraversal>(vertex);
                 return py::make_iterator(iterator.begin(), iterator.end());
             },
             py::keep_alive<0, 1>(),
             py::arg("vertex_index"))
         .def(
             "get_forward_adjacent_vertex_indices",
             [](const StaticDigraph& self, VertexIndex vertex)
             {
                 auto iterator = self.get_adjacent_vertex_indices<ForwardTraversal>(vertex);
                 return py::make_iterator(iterator.begin(), iterator.end());
             },
             py::keep_alive<0, 1>(),
             py::arg("vertex_index"))
         .def(
             "get_backward_adjacent_vertex_indices",
             [](const StaticDigraph& self, VertexIndex vertex)
             {
                 auto iterator = self.get_adjacent_vertex_indices<BackwardTraversal>(vertex);
                 return py::make_iterator(iterator.begin(), iterator.end());
             },
             py::keep_alive<0, 1>(),
             py::arg("vertex_index"))
         .def(
             "get_forward_adjacent_edges",
             [](const StaticDigraph& self, VertexIndex vertex)
             {
                 auto iterator = self.get_adjacent_edges<ForwardTraversal>(vertex);
                 return py::make_iterator(iterator.begin(), iterator.end());
             },
             py::keep_alive<0, 1>(),
             py::arg("vertex_index"))
         .def(
             "get_backward_adjacent_edges",
             [](const StaticDigraph& self, VertexIndex vertex)
             {
                 auto iterator = self.get_adjacent_edges<BackwardTraversal>(vertex);
                 return py::make_iterator(iterator.begin(), iterator.end());
             },
             py::keep_alive<0, 1>(),
             py::arg("vertex_index"))
         .def(
             "get_forward_adjacent_edge_indices",
             [](const StaticDigraph& self, VertexIndex vertex)
             {
                 auto iterator = self.get_adjacent_edge_indices<ForwardTraversal>(vertex);
                 return py::make_iterator(iterator.begin(), iterator.end());
             },
             py::keep_alive<0, 1>(),
             py::arg("vertex_index"))
         .def(
             "get_backward_adjacent_edge_indices",
             [](const StaticDigraph& self, VertexIndex vertex)
             {
                 auto iterator = self.get_adjacent_edge_indices<BackwardTraversal>(vertex);
                 return py::make_iterator(iterator.begin(), iterator.end());
             },
             py::keep_alive<0, 1>(),
             py::arg("vertex_index"))
         .def("get_vertices", &StaticDigraph::get_vertices, py::return_value_policy::reference_internal)
         .def("get_edges", &StaticDigraph::get_edges, py::return_value_policy::reference_internal)
         .def("get_num_vertices", &StaticDigraph::get_num_vertices)
         .def("get_num_edges", &StaticDigraph::get_num_edges);
}