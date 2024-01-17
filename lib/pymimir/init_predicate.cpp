
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"


void init_predicate(py::module& m)
{
    py::class_<mimir::formalism::PredicateImpl, mimir::formalism::Predicate> predicate(m, "Predicate");

    predicate.def_readonly("id", &mimir::formalism::PredicateImpl::id, "Gets the identifier of the predicate.");
    predicate.def_readonly("name", &mimir::formalism::PredicateImpl::name, "Gets the name of the predicate.");
    predicate.def_readonly("arity", &mimir::formalism::PredicateImpl::arity, "Gets the arity of the predicate.");
    predicate.def_readonly("parameters", &mimir::formalism::PredicateImpl::parameters, "Gets the parameters of the predicate.");
    predicate.def("as_atom", [](const mimir::formalism::Predicate& predicate) { return mimir::formalism::create_atom(predicate, predicate->parameters); }, "Creates a new atom where all terms are variables.");
    predicate.def("__repr__", [](const mimir::formalism::PredicateImpl& predicate) { return "<Predicate '" + predicate.name + "/" + std::to_string(predicate.arity) + "'>"; });

}