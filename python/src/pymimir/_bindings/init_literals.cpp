#include "init_declarations.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;

void init_literals(py::module& m)
{

    auto bind_ground_literal = [&]<typename Tag>(const std::string& class_name, Tag)
    {
        class_<GroundLiteralImpl<Tag>>(m, class_name.c_str())
            .def("__str__", &GroundLiteralImpl<Tag>::str)
            .def("__repr__", &GroundLiteralImpl<Tag>::str)
            .def("get_index", &GroundLiteralImpl<Tag>::get_index)
            .def("get_atom", &GroundLiteralImpl<Tag>::get_atom, py::return_value_policy::reference_internal)
            .def("is_negated", &GroundLiteralImpl<Tag>::is_negated);
    };
    bind_ground_literal("StaticGroundLiteral", Static {});
    bind_ground_literal("FluentGroundLiteral", Fluent {});
    bind_ground_literal("DerivedGroundLiteral", Derived {});

    auto bind_literal = [&]<typename Tag>(const std::string& class_name, Tag)
    {
        class_<LiteralImpl<Tag>>(m, class_name.c_str())
            .def("__str__", &LiteralImpl<Tag>::str)
            .def("__repr__", &LiteralImpl<Tag>::str)
            .def("get_index", &LiteralImpl<Tag>::get_index)
            .def("get_atom", &LiteralImpl<Tag>::get_atom, py::return_value_policy::reference_internal)
            .def("is_negated", &LiteralImpl<Tag>::is_negated);
    };
    bind_literal("StaticLiteral", Static {});
    bind_literal("FluentLiteral", Fluent {});
    bind_literal("DerivedLiteral", Derived {});
}