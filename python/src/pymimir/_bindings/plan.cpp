#include "init_declarations.hpp"
#include "pymimir.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace pymimir;

void init_plan(py::module& m)
{
    class_<Plan>(m, "Plan")
        .def("__len__", [](const Plan& arg) { return arg.get_actions().size(); })
        .def("get_actions", &Plan::get_actions)
        .def("get_cost", &Plan::get_cost);
}
