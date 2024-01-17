
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"
#include "mimir/pddl/parsers.hpp"
#include "mimir/search/breadth_first_search.hpp"
#include "mimir/search/eager_astar_search.hpp"
#include "mimir/search/search_base.hpp"


using namespace py::literals;

void init_search(py::module& m)
{
    py::class_<mimir::planners::SearchBase, mimir::planners::Search> search(m, "Search");

    search.def("plan",
               [](const mimir::planners::Search& search)
               {
                   mimir::formalism::ActionList plan;
                   const auto result = search->plan(plan);
                   return std::make_pair(result == mimir::planners::SearchResult::SOLVED, plan);
               });
    search.def("abort", &mimir::planners::SearchBase::abort);
    search.def("set_initial_state", &mimir::planners::SearchBase::set_initial_state, "state"_a, "Sets the initial state of the search.");
    search.def("register_callback",
               &mimir::planners::SearchBase::register_handler,
               "callback_function"_a,
               "The callback function will be invoked as the search algorithm progresses.");
    search.def("get_statistics", &mimir::planners::SearchBase::get_statistics, "Get statistics of the search so far.");

    py::class_<mimir::planners::BreadthFirstSearchImpl, mimir::planners::BreadthFirstSearch> breadth_first_search(m, "BreadthFirstSearch", search);

    breadth_first_search.def(py::init(&mimir::planners::create_breadth_first_search),
                             "problem"_a,
                             "successor_generator"_a,
                             "Creates a breadth-first search object.");

    py::class_<mimir::planners::EagerAStarSearchImpl, mimir::planners::EagerAStarSearch> eager_astar_search(m, "AStarSearch", search);

    eager_astar_search.def(py::init(&mimir::planners::create_eager_astar),
                           "problem"_a,
                           "successor_generator"_a,
                           "heuristic"_a,
                           "open_list"_a,
                           "Creates an A* search object.");
}