
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/literal_grounder.hpp"

using namespace py::literals;

using namespace mimir::planners;

void init_literal_grounder(py::module& m)
{
    py::class_<LiteralGrounder, std::shared_ptr<LiteralGrounder>> literal_grounder(m, "LiteralGrounder");

    literal_grounder.def(py::init([](const mimir::formalism::ProblemDescription& problem, const mimir::formalism::AtomList& atom_list)
                                  { return std::make_shared<LiteralGrounder>(problem, atom_list); }),
                         "problem"_a,
                         "atom_list"_a);
    literal_grounder.def("ground",
                         &LiteralGrounder::ground,
                         "state"_a,
                         "Gets a list of instantiations of the associated atom list that are true in the given state.");
    literal_grounder.def("__repr__", [](const LiteralGrounder& grounder) { return "<LiteralGrounder>"; });
}