
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"
#include "to_string.hpp"

using namespace py::literals;

void init_atom(py::module& m)
{
    py::class_<mimir::formalism::AtomImpl, mimir::formalism::Atom> atom(m, "Atom");

    atom.def_readonly("predicate", &mimir::formalism::AtomImpl::predicate, "Gets the predicate of the atom");
    atom.def_readonly("terms", &mimir::formalism::AtomImpl::arguments, "Gets the terms of the atom");
    atom.def("replace_term", &mimir::formalism::replace_term, "index"_a, "object"_a, "Replaces a term in the atom");
    atom.def("matches_state", &mimir::formalism::matches_any_in_state, "state"_a, "Tests if any atom matches an atom in the state");
    atom.def("get_name", [](const mimir::formalism::AtomImpl& atom) { return to_string(atom); }, "Gets the name of the atom.");
    atom.def("__repr__", [](const mimir::formalism::AtomImpl& atom) { return "<Atom '" + to_string(atom) + "'>"; });
}