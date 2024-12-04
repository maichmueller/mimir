# Import all classes for better IDE support

# Common
from _pymimir import *
from .hints import *


def parse_pddl(domain_path, problem_path):
    parser = PDDLParser(domain_path, problem_path)
    return parser.get_domaiin(), parser.get_problem(), parser.get_pddl_repositories()
