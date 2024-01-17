
#include "init.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"

void init_implication(py::module& m)
{
    py::class_<mimir::formalism::Implication, std::shared_ptr<mimir::formalism::Implication>> implication(m, "Implication");

    implication.def_readonly("antecedent", &mimir::formalism::Implication::antecedent, "Gets the antecedent of the implication.");
    implication.def_readonly("consequence", &mimir::formalism::Implication::consequence, "Gets the consequence of the implication.");
    implication.def("__repr__", [](const mimir::formalism::Implication& implication) { return "<Implication>"; });
}