

#include "sandbox.hpp"

#include "mimir/datasets/state_space.hpp"
#include "mimir/mimir.hpp"
#include "mimir/utils/utils.hpp"
#include "utils.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <memory>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <range/v3/all.hpp>
#include <ranges>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace py = pybind11;

using namespace mimir;

int main()
{
    py::scoped_interpreter guard {};
    std::vector<std::string> v = { "a", "b", "c", "d" };
    auto parser = mimir::PDDLParser("data/blocks_4/domain.pddl", "data/blocks_4/test_problem.pddl");
    auto problem = parser.get_problem();
    auto state_space = mimir::StateSpace::create(problem, parser.get_pddl_factories());
    auto init_state = state_space->get_initial_state();
    py::list lst = pymimir::insert_into_list(pymimir::insert_into_list(init_state.get_satisfied_literals(problem->get_goal_condition<mimir::Fluent>())),
                                             init_state.get_satisfied_literals(problem->get_goal_condition<mimir::Derived>()));
    for (auto&& elem : lst)
    {
        std::cout << py::str(elem).cast<std::string>() << '\n';
    }
};