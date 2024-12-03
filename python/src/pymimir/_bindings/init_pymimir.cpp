#include "init_declarations.hpp"
#include "mimir/common/itertools.hpp"
#include "opaque_types.hpp"
#include "pymimir.hpp"
#include "trampolines.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace pymimir;

void init_pymimir(py::module& m)
{
    // first the cental registrations
    register_classes(m);
    init_enums(m);
    // and the lists
    init_lists(m);
    // now all the individual class detail bindings...
    init_requirements(m);
    init_object(m);
    init_variable(m);
    init_term(m);
    init_predicates(m);
    init_atoms(m);
    init_pddl_repositories(m);
    init_ground_atoms(m);
    init_literals(m);
    init_axiom(m);
    init_numeric_fluent(m);
    init_effects(m);
    init_cista_types(m);
    init_function(m);
    init_function_expression(m);
    init_ground_function_expression(m);
    init_optimization_metric(m);
    init_actions(m);
    init_domain(m);
    init_problem(m);
    init_parser(m);
    init_state(m);
    init_strips_action_precondition(m);
    init_conditional_effect(m);
    init_applicable_action_generator(m);
    init_heuristics(m);
    init_algorithms(m);
    init_state_space(m);
    init_abstraction(m);
    init_static_digraph(m);
    init_nauty_wrappers(m);
    init_static_vertexcolored_graph(m);
    init_tuple_graph(m);

    // ObjectGraph
    m.def("create_object_graph",
          &create_object_graph,
          py::arg("color_function"),
          py::arg("pddl_repositories"),
          py::arg("problem"),
          py::arg("state"),
          py::arg("state_index") = 0,
          py::arg("mark_true_goal_literals") = false,
          py::arg("pruning_strategy") = ObjectGraphPruningStrategy(),
          "Creates an object graph based on the provided parameters");
    // Color Refinement
    class_<color_refinement::Certificate>(m, "CertificateColorRefinement")
        .def("__eq__", [](const color_refinement::Certificate& lhs, const color_refinement::Certificate& rhs) { return lhs == rhs; })
        .def("__hash__", [](const color_refinement::Certificate& self) { return std::hash<color_refinement::Certificate>()(self); })
        .def("__str__",
             [](const color_refinement::Certificate& self)
             {
                 auto os = std::stringstream();
                 os << self;
                 return os.str();
             })
        // Returning canonical compression functions does not work due to unhashable type list.
        //.def("get_canonical_configuration_compression_function", &color_refinement::Certificate::get_canonical_compression_function)
        .def("get_canonical_coloring", &color_refinement::Certificate::get_canonical_coloring);

    m.def("compute_certificate_color_refinement",
          &color_refinement::compute_certificate<StaticVertexColoredDigraph>,
          py::arg("graph"),
          "Creates color refinement certificate");

    // K-FWL
    for_each_index<size_t, 2, 3, 4>(
        [&]<size_t K>(std::integral_constant<size_t, K>, const std::string& class_name = fmt::format("Certificate{}FWL", K))
        {
            using CertificateK = kfwl::Certificate<K>;

            class_<CertificateK>(m, class_name.c_str())
                .def("__eq__", [](const CertificateK& lhs, const CertificateK& rhs) { return lhs == rhs; })
                .def("__hash__", [](const CertificateK& self) { return std::hash<CertificateK>()(self); })
                .def("__str__",
                     [](const CertificateK& self)
                     {
                         auto os = std::stringstream();
                         os << self;
                         return os.str();
                     })
                // Returning canonical compression functions does not work due to unhashable type list.
                //.def("get_canonical_isomorphic_type_compression_function", &CertificateK::get_canonical_isomorphic_type_compression_function)
                //.def("get_canonical_configuration_compression_function", &CertificateK::get_canonical_configuration_compression_function)
                .def("get_canonical_coloring", &CertificateK::get_canonical_coloring);
        });

    class_<kfwl::IsomorphismTypeCompressionFunction>(m, "IsomorphismTypeCompressionFunction")  //
        .def(py::init<>());

    for_each_index<size_t, 2, 3, 4>(
        [&]<size_t K>(std::integral_constant<size_t, K>, const std::string& function_name = fmt::format("compute_certificate_{}fwl", K))
        {
            m.def(
                function_name.c_str(),
                [](const StaticVertexColoredDigraph& graph, kfwl::IsomorphismTypeCompressionFunction& iso_type_function)
                { return kfwl::compute_certificate<K>(graph, iso_type_function); },
                py::arg("static_vertex_colored_digraph"),
                py::arg("isomorphism_type_compression_function"));
        });
}