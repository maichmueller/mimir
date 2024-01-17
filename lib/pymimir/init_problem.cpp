
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/grounded_successor_generator.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"


using namespace py::literals;

void init_problem(py::module& m)
{
    py::class_<mimir::formalism::ProblemImpl, mimir::formalism::ProblemDescription> problem(m, "Problem");


    problem.def_readonly("name", &mimir::formalism::ProblemImpl::name, "Gets the name of the problem.");
    problem.def_readonly("domain", &mimir::formalism::ProblemImpl::domain, "Gets the domain associated with the problem.");
    problem.def_readonly("objects", &mimir::formalism::ProblemImpl::objects, "Gets the objects of the problem.");
    problem.def_readonly("initial", &mimir::formalism::ProblemImpl::initial, "Gets the initial atoms of the problem.");
    problem.def_readonly("goal", &mimir::formalism::ProblemImpl::goal, "Gets the goal of the problem.");
    problem.def("replace_initial", &mimir::formalism::ProblemImpl::replace_initial, "initial"_a, "Gets a new object with the given initial atoms.");
    problem.def("create_state", [](const mimir::formalism::ProblemDescription& problem, const mimir::formalism::AtomList& atom_list) { return mimir::formalism::create_state(atom_list, problem); }, "Creates a new state given a list of atoms.");
    problem.def("get_encountered_atoms", &mimir::formalism::ProblemImpl::get_encountered_atoms, "Gets all atoms seen so far.");
    problem.def("__repr__", [](const mimir::formalism::ProblemImpl& problem) { return "<Problem '" + problem.name + "'>"; });

}