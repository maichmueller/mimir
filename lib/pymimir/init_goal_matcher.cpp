
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/complete_state_space.hpp"
#include "mimir/generators/goal_matcher.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"


using namespace py::literals;

void init_goal_matcher(py::module& m)
{
    py::class_<mimir::planners::GoalMatcher, std::shared_ptr<mimir::planners::GoalMatcher>> goal_matcher(m, "GoalMatcher");

    goal_matcher.def(py::init([](const mimir::planners::CompleteStateSpace& state_space) { return std::make_shared<mimir::planners::GoalMatcher>(state_space); }), "state_space"_a);
    goal_matcher.def("best_match", py::overload_cast<const mimir::formalism::AtomList&>(&mimir::planners::GoalMatcher::best_match), "goal"_a);
    goal_matcher.def("best_match", py::overload_cast<const mimir::formalism::State&, const mimir::formalism::AtomList&>(&mimir::planners::GoalMatcher::best_match), "state"_a, "goal"_a);
    goal_matcher.def("__repr__", [](const mimir::planners::GoalMatcher& goal_matcher) { return "<GoalMatcher>"; });

}