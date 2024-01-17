#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/complete_state_space.hpp"
#include "mimir/generators/goal_matcher.hpp"
#include "mimir/generators/grounded_successor_generator.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"
#include "mimir/generators/successor_generator.hpp"
#include "mimir/generators/successor_generator_factory.hpp"
#include "mimir/pddl/parsers.hpp"
#include "mimir/search/breadth_first_search.hpp"
#include "mimir/search/eager_astar_search.hpp"
#include "mimir/search/heuristics/h1_heuristic.hpp"
#include "mimir/search/heuristics/h2_heuristic.hpp"
#include "mimir/search/heuristics/heuristic_base.hpp"
#include "mimir/search/openlists/open_list_base.hpp"
#include "mimir/search/openlists/priority_queue_open_list.hpp"
#include "mimir/search/search_base.hpp"

#include <Python.h>
#include <memory>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;
using namespace py::literals;

// Definitions
PYBIND11_MODULE(pymimir, m)
{
    m.doc() = "Mimir: Lifted PDDL parsing and expansion library.";

    // calls to the initialization functions
    init_action(m);
    init_action_schema(m);
    init_atom(m);
    init_domain(m);
    init_domain_parser(m);
    init_goal_matcher(m);
    init_heuristic(m);
    init_implication(m);
    init_literal(m);
    init_literal_grounder(m);
    init_object(m);
    init_open_list(m);
    init_predicate(m);
    init_problem(m);
    init_problem_parser(m);
    init_search(m);
    init_state(m);
    init_successor_generator(m);
    init_transition(m);
    init_type(m);
}
