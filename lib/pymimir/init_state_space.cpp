
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/complete_state_space.hpp"
#include "mimir/generators/goal_matcher.hpp"
#include "mimir/generators/grounded_successor_generator.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"

using namespace py::literals;

void init_state_space(py::module& m)
{
    py::class_<mimir::planners::CompleteStateSpaceImpl, mimir::planners::CompleteStateSpace> state_space(m, "StateSpace");

    state_space.def_static("new", &mimir::planners::create_complete_state_space, "problem"_a, "successor_generator"_a, "max_expanded"_a = 1'000'000);
    state_space.def_readonly("domain", &mimir::planners::CompleteStateSpaceImpl::domain, "Gets the domain associated with the state space.");
    state_space.def_readonly("problem", &mimir::planners::CompleteStateSpaceImpl::problem, "Gets the problem associated with the state space.");
    state_space.def("get_states", &mimir::planners::CompleteStateSpaceImpl::get_states, "Gets all states in the state space.");
    state_space.def("get_initial_state", &mimir::planners::CompleteStateSpaceImpl::get_initial_state, "Gets the initial state of the state space.");
    state_space.def("get_goal_states", &mimir::planners::CompleteStateSpaceImpl::get_goal_states, "Gets all goal states of the state space.");
    state_space.def("get_distance_from_initial_state",
                    &mimir::planners::CompleteStateSpaceImpl::get_distance_from_initial_state,
                    "state"_a,
                    "Gets the distance from the initial state to the given state.");
    state_space.def("get_distance_to_goal_state",
                    &mimir::planners::CompleteStateSpaceImpl::get_distance_to_goal_state,
                    "state"_a,
                    "Gets the distance from the given state to the closest goal state.");
    state_space.def("get_distance_between_states",
                    &mimir::planners::CompleteStateSpaceImpl::get_distance_between_states,
                    "from_state"_a,
                    "to_state"_a,
                    "Gets the distance between the \"from state\" to the \"to state\".");
    state_space.def("get_longest_distance_to_goal_state",
                    &mimir::planners::CompleteStateSpaceImpl::get_longest_distance_to_goal_state,
                    "Gets the longest distance from a state to its closest goal state.");
    state_space.def("get_forward_transitions",
                    &mimir::planners::CompleteStateSpaceImpl::get_forward_transitions,
                    "state"_a,
                    "Gets the possible forward transitions of the given state.");
    state_space.def("get_backward_transitions",
                    &mimir::planners::CompleteStateSpaceImpl::get_backward_transitions,
                    "state"_a,
                    "Gets the possible backward transitions of the given state.");
    state_space.def("get_unique_id",
                    &mimir::planners::CompleteStateSpaceImpl::get_unique_index_of_state,
                    "state"_a,
                    "Gets an unique identifier of the given from 0 to N - 1, where N is the number of states in the state space.");
    state_space.def("is_dead_end_state",
                    &mimir::planners::CompleteStateSpaceImpl::is_dead_end_state,
                    "state"_a,
                    "Tests whether the given state is a dead end state.");
    state_space.def("is_goal_state", &mimir::planners::CompleteStateSpaceImpl::is_goal_state, "state"_a, "Tests whether the given state is a goal state.");
    state_space.def("sample_state", &mimir::planners::CompleteStateSpaceImpl::sample_state, "Gets a uniformly random state from the state space.");
    state_space.def("sample_state_with_distance_to_goal",
                    &mimir::planners::CompleteStateSpaceImpl::sample_state_with_distance_to_goal,
                    "distance"_a,
                    "Gets a uniformly random state from the state space with the given distance to its closest goal state.");
    state_space.def("sample_dead_end_state",
                    &mimir::planners::CompleteStateSpaceImpl::sample_dead_end_state,
                    "Gets a uniformly random dead end state from the state space.");
    state_space.def("num_states", &mimir::planners::CompleteStateSpaceImpl::num_states, "Gets the number of states in the state space.");
    state_space.def("num_dead_end_states",
                    &mimir::planners::CompleteStateSpaceImpl::num_dead_end_states,
                    "Gets the number of dead end states in the state space.");
    state_space.def("num_goal_states", &mimir::planners::CompleteStateSpaceImpl::num_goal_states, "Gets the number of goal states in the state space.");
    state_space.def("num_transitions", &mimir::planners::CompleteStateSpaceImpl::num_transitions, "Gets the number of transitions in the state space.");
    state_space.def("__repr__",
                    [](const mimir::planners::CompleteStateSpaceImpl& state_space)
                    { return "<StateSpace '" + state_space.problem->name + ": " + std::to_string(state_space.num_states()) + " states'>"; });
}