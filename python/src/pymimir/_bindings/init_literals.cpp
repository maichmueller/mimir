#include "init_declarations.hpp"
#include "mimir/common/itertools.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace pymimir;

void init_literals(py::module& m)
{
    for_each_tag(
        [&]<typename Tag>(Tag, const std::string& class_name = tag_name<Tag>() + "GroundLiteral")
        {
            class_<GroundLiteralImpl<Tag>>(m, class_name.c_str())
                .def("__str__", [](const GroundLiteralImpl<Tag>& self) { return fmt::format("{}", self); })
                .def("__str__", [](const GroundLiteralImpl<Tag>& self) { return fmt::format("{}", self); })
                .def("get_index", &GroundLiteralImpl<Tag>::get_index)
                .def("get_atom", &GroundLiteralImpl<Tag>::get_atom, py::return_value_policy::reference_internal)
                .def("is_negated", &GroundLiteralImpl<Tag>::is_negated);
        });

    for_each_tag(
        [&]<typename Tag>(Tag, const std::string& class_name = tag_name<Tag>() + "Literal")
        {
            class_<LiteralImpl<Tag>>(m, class_name.c_str())
                .def("__str__", [](const LiteralImpl<Tag>& self) { return fmt::format("{}", self); })
                .def("__str__", [](const LiteralImpl<Tag>& self) { return fmt::format("{}", self); })
                .def("get_index", &LiteralImpl<Tag>::get_index)
                .def("get_atom", &LiteralImpl<Tag>::get_atom, py::return_value_policy::reference_internal)
                .def("is_negated", &LiteralImpl<Tag>::is_negated);
        });
}