
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"

void init_action_schema(py::module& m)
{
    py::class_<mimir::formalism::ActionSchemaImpl, mimir::formalism::ActionSchema> action_schema(m, "ActionSchema");


    action_schema.def_readonly("name", &mimir::formalism::ActionSchemaImpl::name, "Gets the name of the action schema.");
    action_schema.def_readonly("arity", &mimir::formalism::ActionSchemaImpl::arity, "Gets the arity of the action schema.");
    action_schema.def_readonly("parameters", &mimir::formalism::ActionSchemaImpl::parameters, "Gets the parameters of the action schema.");
    action_schema.def_readonly("precondition", &mimir::formalism::ActionSchemaImpl::precondition, "Gets the precondition of the action schema.");
    action_schema.def_readonly("effect", &mimir::formalism::ActionSchemaImpl::unconditional_effect, "Gets the unconditional effect of the action schema.");
    action_schema.def_readonly("conditional_effect", &mimir::formalism::ActionSchemaImpl::conditional_effect, "Gets the conditional effect of the action schema.");
    action_schema.def("__repr__", [](const mimir::formalism::ActionSchemaImpl& action_schema) { return "<ActionSchema '" + action_schema.name + "/" + std::to_string(action_schema.arity) + "'>"; });

}