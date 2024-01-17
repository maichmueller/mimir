
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"

void init_domain(py::module& m)
{
    py::class_<mimir::formalism::DomainImpl, mimir::formalism::DomainDescription> domain(m, "Domain");

    domain.def_readonly("name", &mimir::formalism::DomainImpl::name, "Gets the name of the domain.");
    domain.def_readonly("requirements", &mimir::formalism::DomainImpl::requirements, "Gets the requirements of the domain.");
    domain.def_readonly("types", &mimir::formalism::DomainImpl::types, "Gets the types of the domain.");
    domain.def_readonly("constants", &mimir::formalism::DomainImpl::constants, "Gets the constants of the domain.");
    domain.def_readonly("predicates", &mimir::formalism::DomainImpl::predicates, "Gets the predicates of the domain.");
    domain.def_readonly("static_predicates", &mimir::formalism::DomainImpl::static_predicates, "Gets the static predicates of the domain.");
    domain.def_readonly("action_schemas", &mimir::formalism::DomainImpl::action_schemas, "Gets the action schemas of the domain.");
    domain.def("get_type_map", &mimir::formalism::DomainImpl::get_type_map, "Gets a dictionary mapping type name to type object.");
    domain.def("get_predicate_name_map", &mimir::formalism::DomainImpl::get_predicate_name_map, "Gets a dictionary mapping predicate name to predicate object.");
    domain.def("get_predicate_id_map", &mimir::formalism::DomainImpl::get_predicate_id_map, "Gets a dictionary mapping predicate identifier to predicate object.");
    domain.def("get_constant_map", &mimir::formalism::DomainImpl::get_constant_map, "Gets a dictionary mapping constant name to constant object.");
    domain.def("__repr__", [](const mimir::formalism::DomainImpl& domain) { return "<Domain '" + domain.name + "'>"; });
}