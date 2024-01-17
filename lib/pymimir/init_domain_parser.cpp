
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"
#include "mimir/pddl/parsers.hpp"

using namespace py::literals;


std::shared_ptr<mimir::parsers::DomainParser> create_domain_parser(const std::string& path) { return std::make_shared<mimir::parsers::DomainParser>(path); }


void init_domain_parser(py::module& m)
{
    py::class_<mimir::parsers::DomainParser, std::shared_ptr<mimir::parsers::DomainParser>> domain_parser(m, "DomainParser");

    domain_parser.def(py::init(&create_domain_parser), "path"_a);
    domain_parser.def("parse", (mimir::formalism::DomainDescription (mimir::parsers::DomainParser::*)()) &mimir::parsers::DomainParser::parse, "Parses the associated file and creates a new domain.");

}