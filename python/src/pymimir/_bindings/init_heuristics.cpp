#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"
#include "trampolines.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;


void init_heuristics(py::module& m)
{
    /* Heuristics */
    class_<IHeuristic, IPyHeuristic, std::shared_ptr<IHeuristic>>("IHeuristic").def(py::init<>());
    class_<BlindHeuristic, IHeuristic, std::shared_ptr<BlindHeuristic>>("BlindHeuristic").def(py::init<>());
}