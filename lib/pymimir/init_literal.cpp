
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"
#include "mimir/pddl/parsers.hpp"
#include "to_string.hpp"


void init_literal(py::module& m)
{
    py::class_<mimir::formalism::LiteralImpl, mimir::formalism::Literal> literal(m, "Literal");

    literal.def_readonly("atom", &mimir::formalism::LiteralImpl::atom, "Gets the atom of the literal.");
    literal.def_readonly("negated", &mimir::formalism::LiteralImpl::negated, "Returns whether the literal is negated.");
    literal.def("__repr__",
                [](const mimir::formalism::LiteralImpl& literal)
                {
                    const auto prefix = (literal.negated ? std::string("not ") : std::string(""));
                    const auto name = to_string(*literal.atom);
                    return "<Literal '" + prefix + name + "'>";
                });
}