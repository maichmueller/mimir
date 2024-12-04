#include "init_declarations.hpp"
#include "mimir/common/itertools.hpp"
#include "pymimir.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace pymimir;

void init_color_refinement(py::module& m)
{
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

    class_<kfwl::IsomorphismTypeCompressionFunction>(m, "IsomorphismTypeCompressionFunction")  //
        .def(py::init<>());

    for_each_index<2, 3, 4>(
        [&]<size_t K>(std::integral_constant<size_t, K>)
        {
            using CertificateK = kfwl::Certificate<K>;

            std::string class_name = fmt::format("Certificate{}FWL", K);
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

            const auto function_name = fmt::format("compute_certificate_{}fwl", K);
            m.def(
                function_name.c_str(),
                [](const StaticVertexColoredDigraph& graph, kfwl::IsomorphismTypeCompressionFunction& iso_type_function)
                { return kfwl::compute_certificate<K>(graph, iso_type_function); },
                py::arg("static_vertex_colored_digraph"),
                py::arg("isomorphism_type_compression_function"));
        });

    m.def("compute_certificate_color_refinement",
          &color_refinement::compute_certificate<StaticVertexColoredDigraph>,
          py::arg("graph"),
          "Creates color refinement certificate");
}