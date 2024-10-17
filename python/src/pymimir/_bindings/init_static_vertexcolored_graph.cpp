#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;
using namespace mimir;
using namespace pymimir;

void init_static_vertexcolored_graph(py::module& m)
{
    // ColorFunction
    py::class_<ColorFunction>(m, "ColorFunction")  //
        .def("get_color_name", [](const ColorFunction& self, Color color) -> const std::string& { return self.get_color_name(color); }, py::arg("color"));

    // ProblemColorFunction
    py::class_<ProblemColorFunction, ColorFunction>(m, "ProblemColorFunction")  //
        .def(py::init<Problem>(), py::arg("problem"))
        .def(
            "get_color",
            [](const ProblemColorFunction& self, Object object) -> Color { return self.get_color(object); },
            py::arg("object"))
        .def(
            "get_color",
            [](const ProblemColorFunction& self, GroundAtom<Static> atom, int pos) -> Color { return self.get_color(atom, pos); },
            py::arg("atom"),
            py::arg("position"))
        .def(
            "get_color",
            [](const ProblemColorFunction& self, GroundAtom<Fluent> atom, int pos) -> Color { return self.get_color(atom, pos); },
            py::arg("atom"),
            py::arg("position"))
        .def(
            "get_color",
            [](const ProblemColorFunction& self, GroundAtom<Derived> atom, int pos) -> Color { return self.get_color(atom, pos); },
            py::arg("atom"),
            py::arg("position"))
        .def(
            "get_color",
            [](const ProblemColorFunction& self, State state, GroundLiteral<Static> literal, int pos, bool mark_true_goal_literal) -> Color
            { return self.get_color(state, literal, pos, mark_true_goal_literal); },
            py::arg("state"),
            py::arg("literal"),
            py::arg("position"),
            py::arg("mark_true_goal_literal"))
        .def(
            "get_color",
            [](const ProblemColorFunction& self, State state, GroundLiteral<Fluent> literal, int pos, bool mark_true_goal_literal) -> Color
            { return self.get_color(state, literal, pos, mark_true_goal_literal); },
            py::arg("state"),
            py::arg("literal"),
            py::arg("position"),
            py::arg("mark_true_goal_literal"))
        .def(
            "get_color",
            [](const ProblemColorFunction& self, State state, GroundLiteral<Derived> literal, int pos, bool mark_true_goal_literal) -> Color
            { return self.get_color(state, literal, pos, mark_true_goal_literal); },
            py::arg("state"),
            py::arg("literal"),
            py::arg("position"),
            py::arg("mark_true_goal_literal"))
        .def("get_problem", &ProblemColorFunction::get_problem, py::return_value_policy::reference_internal)
        .def("get_name_to_color", &ProblemColorFunction::get_name_to_color, py::return_value_policy::reference_internal)
        .def("get_color_to_name", &ProblemColorFunction::get_color_to_name, py::return_value_policy::reference_internal);

    // ColoredVertex
    py::class_<ColoredVertex>(m, "ColoredVertex")
        .def("__eq__", &ColoredVertex::operator==)
        .def("__hash__", [](const ColoredVertex& self) { return std::hash<ColoredVertex>()(self); })
        .def_property_readonly("index", &ColoredVertex::get_index)
        .def_property_readonly("color", [](const ColoredVertex& self) { return get_color(self); });

    // StaticVertexColoredDigraph
    py::class_<StaticVertexColoredDigraph>(m, "StaticVertexColoredDigraph")  //
        .def(py::init<>())
        .def(
            "to_string",
            [](const StaticVertexColoredDigraph& self, const ColorFunction& color_function)
            {
                std::stringstream ss;
                ss << std::make_tuple(std::cref(self), std::cref(color_function));
                return ss.str();
            },
            py::arg("color_function"))
        .def(
            "add_vertex",
            [](StaticVertexColoredDigraph& self, Color color) -> VertexIndex { return self.add_vertex(color); },
            py::arg("color"))
        .def(
            "add_directed_edge",
            [](StaticVertexColoredDigraph& self, VertexIndex source, VertexIndex target) -> EdgeIndex { return self.add_directed_edge(source, target); },
            py::arg("source"),
            py::arg("target"))
        .def(
            "add_undirected_edge",
            [](StaticVertexColoredDigraph& self, VertexIndex source, VertexIndex target) -> std::pair<EdgeIndex, EdgeIndex>
            { return self.add_undirected_edge(source, target); },
            py::arg("source"),
            py::arg("target"))
        .def("clear", &StaticVertexColoredDigraph::clear)
        .def(
            "get_forward_adjacent_vertices",
            [](const StaticVertexColoredDigraph& self, VertexIndex vertex)
            {
                auto iterator = self.get_adjacent_vertices<ForwardTraversal>(vertex);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("vertex_index"))
        .def(
            "get_backward_adjacent_vertices",
            [](const StaticVertexColoredDigraph& self, VertexIndex vertex)
            {
                auto iterator = self.get_adjacent_vertices<BackwardTraversal>(vertex);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("vertex_index"))
        .def(
            "get_forward_adjacent_vertex_indices",
            [](const StaticVertexColoredDigraph& self, VertexIndex vertex)
            {
                auto iterator = self.get_adjacent_vertex_indices<ForwardTraversal>(vertex);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("vertex_index"))
        .def(
            "get_backward_adjacent_vertex_indices",
            [](const StaticVertexColoredDigraph& self, VertexIndex vertex)
            {
                auto iterator = self.get_adjacent_vertex_indices<BackwardTraversal>(vertex);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("vertex_index"))
        .def(
            "get_forward_adjacent_edges",
            [](const StaticVertexColoredDigraph& self, VertexIndex vertex)
            {
                auto iterator = self.get_adjacent_edges<ForwardTraversal>(vertex);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("vertex_index"))
        .def(
            "get_backward_adjacent_edges",
            [](const StaticVertexColoredDigraph& self, VertexIndex vertex)
            {
                auto iterator = self.get_adjacent_edges<BackwardTraversal>(vertex);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("vertex_index"))
        .def(
            "get_forward_adjacent_edge_indices",
            [](const StaticVertexColoredDigraph& self, VertexIndex vertex)
            {
                auto iterator = self.get_adjacent_edge_indices<ForwardTraversal>(vertex);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("vertex_index"))
        .def(
            "get_backward_adjacent_edge_indices",
            [](const StaticVertexColoredDigraph& self, VertexIndex vertex)
            {
                auto iterator = self.get_adjacent_edge_indices<BackwardTraversal>(vertex);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("vertex_index"))
        .def("get_vertices", &StaticVertexColoredDigraph::get_vertices, py::return_value_policy::reference_internal)
        .def("get_edges", &StaticVertexColoredDigraph::get_edges, py::return_value_policy::reference_internal)
        .def("get_num_vertices", &StaticVertexColoredDigraph::get_num_vertices)
        .def("get_num_edges", &StaticVertexColoredDigraph::get_num_edges);

    m.def("compute_vertex_colors", &compute_vertex_colors, py::arg("vertex_colored_graph"));

    m.def("compute_sorted_vertex_colors", &compute_sorted_vertex_colors, py::arg("vertex_colored_graph"));

    // DenseNautyGraph
    py::class_<nauty_wrapper::DenseGraph>(m, "DenseNautyGraph")  //
        .def(py::init<>())
        .def(py::init<int>(), py::arg("num_vertices"))
        .def(py::init<StaticVertexColoredDigraph>(), py::arg("digraph"))
        .def("add_edge", &nauty_wrapper::DenseGraph::add_edge, py::arg("source"), py::arg("target"))
        .def("compute_certificate", &nauty_wrapper::DenseGraph::compute_certificate)
        .def("clear", &nauty_wrapper::DenseGraph::clear, py::arg("num_vertices"));

    // SparseNautyGraph
    py::class_<nauty_wrapper::SparseGraph>(m, "SparseNautyGraph")  //
        .def(py::init<>())
        .def(py::init<int>(), py::arg("num_vertices"))
        .def(py::init<StaticVertexColoredDigraph>(), py::arg("digraph"))
        .def("add_edge", &nauty_wrapper::SparseGraph::add_edge, py::arg("source"), py::arg("target"))
        .def("compute_certificate", &nauty_wrapper::SparseGraph::compute_certificate)
        .def("clear", &nauty_wrapper::SparseGraph::clear, py::arg("num_vertices"));
}