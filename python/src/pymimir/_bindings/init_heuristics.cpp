#include "init_declarations.hpp"
#include "pymimir.hpp"
#include "utils.hpp"
#include "trampolines.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;


using namespace pymimir;


void init_heuristics(py::module& m)
{
    /* Heuristics */
    class_<IHeuristic, IPyHeuristic, std::shared_ptr<IHeuristic>>(m, "IHeuristic").def(py::init<>());
    class_<BlindHeuristic, IHeuristic, std::shared_ptr<BlindHeuristic>>(m, "BlindHeuristic").def(py::init<>());
}