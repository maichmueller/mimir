
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"
#include "mimir/pddl/parsers.hpp"

using namespace py::literals;

std::shared_ptr<mimir::parsers::ProblemParser> create_problem_parser(const std::string& path) { return std::make_shared<mimir::parsers::ProblemParser>(path); }

void init_problem_parser(py::module& m)
{
    py::class_<mimir::parsers::ProblemParser, std::shared_ptr<mimir::parsers::ProblemParser>> problem_parser(m, "ProblemParser");

    problem_parser.def(py::init(&create_problem_parser), "path"_a);
    problem_parser.def("parse",
                       (mimir::formalism::ProblemDescription(mimir::parsers::ProblemParser::*)(const mimir::formalism::DomainDescription&))
                           & mimir::parsers::ProblemParser::parse,
                       "Parses the associated file and creates a new problem.");
}