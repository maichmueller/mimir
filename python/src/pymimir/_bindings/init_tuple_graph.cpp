#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace pymimir;

void init_tuple_graph(py::module& m)
{
    // TupleGraphVertex
    class_<TupleGraphVertex>(m, "TupleGraphVertex")  //
        .def_property_readonly("index", &TupleGraphVertex::get_index)
        .def_property_readonly("tuple_index", &TupleGraphVertex::get_tuple_index)
        .def_property_readonly("state_vertices", [](const TupleGraphVertex& self) { return self.get_states(); }, py::keep_alive<0, 1>());
    bind_const_span<std::span<const TupleGraphVertex>>(m, "TupleGraphVertexSpan");
    bind_const_index_grouped_vector<IndexGroupedVector<const TupleGraphVertex>>(m, "TupleGraphVertexIndexGroupedVector");

    // TupleGraph
    class_<TupleGraph>(m, "TupleGraph")  //
        .def("__str__",
             [](const TupleGraph& self)
             {
                 std::stringstream ss;
                 ss << self;
                 return ss.str();
             })
        .def("compute_admissible_chain", py::overload_cast<const GroundAtomList<Fluent>&>(&TupleGraph::compute_admissible_chain))
        .def("compute_admissible_chain", py::overload_cast<const StateList&>(&TupleGraph::compute_admissible_chain))
        .def("get_state_space", &TupleGraph::get_state_space)
        .def("get_tuple_index_mapper", &TupleGraph::get_tuple_index_mapper)
        .def("get_root_state", &TupleGraph::get_root_state, py::keep_alive<0, 1>())
        .def("get_vertices_grouped_by_distance", &TupleGraph::get_vertices_grouped_by_distance, py::return_value_policy::reference_internal)
        .def("get_digraph", &TupleGraph::get_digraph, py::return_value_policy::reference_internal)
        .def("get_states_grouped_by_distance", &TupleGraph::get_states_grouped_by_distance, py::return_value_policy::reference_internal);

    // TupleGraphFactory
    class_<TupleGraphFactory>(m, "TupleGraphFactory")  //
        .def(py::init<std::shared_ptr<StateSpace>, int, bool>(), py::arg("state_space"), py::arg("arity"), py::arg("prune_dominated_tuples") = false)
        .def("create", &TupleGraphFactory::create)
        .def("get_state_space", &TupleGraphFactory::get_state_space)
        .def("get_tuple_index_mapper", &TupleGraphFactory::get_tuple_index_mapper);
}