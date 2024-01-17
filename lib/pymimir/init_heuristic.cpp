
#include "init.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"
#include "mimir/search/heuristics/h1_heuristic.hpp"
#include "mimir/search/heuristics/h2_heuristic.hpp"
#include "mimir/search/heuristics/heuristic_base.hpp"

using namespace py::literals;

void init_heuristic(py::module& m)
{
    py::class_<mimir::planners::HeuristicBase, mimir::planners::Heuristic> heuristic(m, "Heuristic");
    py::class_<mimir::planners::H1Heuristic, std::shared_ptr<mimir::planners::H1Heuristic>> h1_heuristic(m, "H1Heuristic", heuristic);
    py::class_<mimir::planners::H2Heuristic, std::shared_ptr<mimir::planners::H2Heuristic>> h2_heuristic(m, "H2Heuristic", heuristic);


    h1_heuristic.def(py::init(&mimir::planners::create_h1_heuristic), "problem"_a, "successor_generator"_a, "Creates a h1 heuristic function object.");
    h2_heuristic.def(py::init(&mimir::planners::create_h2_heuristic), "problem"_a, "successor_generator"_a, "Creates a h2 heuristic function object.");

}