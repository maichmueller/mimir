
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/grounded_successor_generator.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"
#include "to_string.hpp"

using namespace py::literals;

void init_action(py::module& m)
{
    py::class_<mimir::formalism::ActionImpl, mimir::formalism::Action> action(m, "Action");

    action.def_readonly("problem", &mimir::formalism::ActionImpl::problem, "Gets the problem associated with the action.");
    action.def_readonly("schema", &mimir::formalism::ActionImpl::schema, "Gets the action schema associated with the action.");
    action.def_readonly("cost", &mimir::formalism::ActionImpl::cost, "Gets the cost of the action.");
    action.def("get_arguments", &mimir::formalism::ActionImpl::get_arguments, "Gets the arguments of the action.");
    action.def("get_precondition", &mimir::formalism::ActionImpl::get_precondition, "Gets the precondition of the action.");
    action.def("get_effect", &mimir::formalism::ActionImpl::get_unconditional_effect, "Gets the unconditional effect of the action.");
    action.def("get_conditional_effect", &mimir::formalism::ActionImpl::get_conditional_effect, "Gets the conditional effect of the action.");
    action.def("get_name", [](const mimir::formalism::ActionImpl& action) { return to_string(action); }, "Gets the name of the action.");
    action.def("is_applicable", &mimir::formalism::is_applicable, "state"_a, "Tests whether the action is applicable in the state.");
    action.def("apply", &mimir::formalism::apply, "state"_a, "Creates a new state state based on the given state and the effect of the action.");
    action.def("__repr__", [](const mimir::formalism::ActionImpl& action) { return "<Action '" + to_string(action) + "'>"; });
}