#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "variants.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;
using namespace mimir;

void init_pymimir(py::module& m)
{
    // order is important either for dependencies in return types of bound functions or for stubgen!
    init_enums(m);
    init_requirements(m);
    init_object(m);
    init_variable(m);
    init_termobject(m);
    init_termvariable(m);
    init_termvariant(m);
    init_predicates(m);
    init_atoms(m);
    init_pddl_factories(m);
    init_ground_atoms(m);
    init_literals(m);
    init_axiom(m);
    init_numeric_fluent(m);
    init_effects(m);
    init_flatbitset(m);
    init_function(m);
    init_function_expression(m);
    init_ground_function_expression(m);
    init_optimization_metric(m);
    init_actions(m);

    init_domain(m);
    init_problem(m);

    py::class_<PDDLParser>(m, "PDDLParser")  //
        .def(py::init<std::string, std::string>(), py::arg("domain_path"), py::arg("problem_path"))
        .def("get_domain", &PDDLParser::get_domain, py::return_value_policy::reference_internal)
        .def("get_problem", &PDDLParser::get_problem, py::return_value_policy::reference_internal)
        .def("get_pddl_factories", &PDDLParser::get_pddl_factories);

    init_state(m);
    init_strips_action_precondition(m);
    init_conditional_effect(m);
    init_aag(m);
    init_heuristics(m);
    init_algorithms(m);
    init_state_space(m);
    init_abstraction(m);
    init_static_digraph(m);
    init_nauty_wrappers(m);
    init_static_vertexcolored_graph(m);
    init_tuple_graph(m);

    py::class_<ObjectGraphPruningStrategy>(m, "ObjectGraphPruningStrategy");

    // ObjectGraph
    m.def("create_object_graph",
          &create_object_graph,
          py::arg("color_function"),
          py::arg("pddl_factories"),
          py::arg("problem"),
          py::arg("state"),
          py::arg("state_index") = 0,
          py::arg("mark_true_goal_literals") = false,
          py::arg("pruning_strategy") = ObjectGraphPruningStrategy(),
          "Creates an object graph based on the provided parameters");
    // Color Refinement
    py::class_<color_refinement::Certificate>(m, "CertificateColorRefinement")
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
    auto bind_kfwl_certificate = [&]<size_t K>(const std::string& class_name, std::integral_constant<size_t, K>)
    {
        using CertificateK = kfwl::Certificate<K>;

        py::class_<CertificateK>(m, class_name.c_str())
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
    };
    bind_kfwl_certificate("Certificate2FWL", std::integral_constant<size_t, 2> {});
    bind_kfwl_certificate("Certificate3FWL", std::integral_constant<size_t, 3> {});
    bind_kfwl_certificate("Certificate4FWL", std::integral_constant<size_t, 4> {});

    py::class_<kfwl::IsomorphismTypeCompressionFunction>(m, "IsomorphismTypeCompressionFunction")  //
        .def(py::init<>());

    auto bind_compute_kfwl_certificate = [&]<size_t K>(const std::string& function_name, std::integral_constant<size_t, K>)
    {
        m.def(
            function_name.c_str(),
            [](const StaticVertexColoredDigraph& graph, kfwl::IsomorphismTypeCompressionFunction& iso_type_function)
            { return kfwl::compute_certificate<K>(graph, iso_type_function); },
            py::arg("static_vertex_colored_digraph"),
            py::arg("isomorphism_type_compression_function"));
    };
    bind_compute_kfwl_certificate("compute_certificate_2fwl", std::integral_constant<size_t, 2> {});
    bind_compute_kfwl_certificate("compute_certificate_3fwl", std::integral_constant<size_t, 3> {});
    bind_compute_kfwl_certificate("compute_certificate_4fwl", std::integral_constant<size_t, 4> {});
}