
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"

void init_type(py::module& m)
{
    py::class_<mimir::formalism::TypeImpl, mimir::formalism::Type> type(m, "Type");

    type.def_readonly("name", &mimir::formalism::TypeImpl::name, "Gets the name of the type.");
    type.def_readonly("base", &mimir::formalism::TypeImpl::base, "Gets the base type of the type.");
    type.def("__repr__",
             [](const mimir::formalism::TypeImpl& type)
             { return type.base ? ("<Type '" + type.name + " : " + type.base->name + "'>") : ("<Type '" + type.name + "'>"); });
}