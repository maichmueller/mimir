
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/goal_matcher.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"
#include "to_string.hpp"




void init_transition(py::module& m)
{
    py::class_<mimir::formalism::TransitionImpl, mimir::formalism::Transition> transition(m, "Transition");

    transition.def_readonly("source", &mimir::formalism::TransitionImpl::source_state, "Gets the source of the transition.");
    transition.def_readonly("target", &mimir::formalism::TransitionImpl::target_state, "Gets the target of the transition.");
    transition.def_readonly("action", &mimir::formalism::TransitionImpl::action, "Gets the action associated with the transition.");
    transition.def("__repr__", [](const mimir::formalism::TransitionImpl& transition) { return "<Transition '" + to_string(*transition.action) + "'>"; });

}