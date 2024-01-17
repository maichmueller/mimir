
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/grounded_successor_generator.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"
#include "mimir/generators/successor_generator.hpp"
#include "mimir/generators/successor_generator_factory.hpp"

using namespace py::literals;

std::shared_ptr<mimir::planners::LiftedSuccessorGenerator> create_lifted_successor_generator(const mimir::formalism::ProblemDescription& problem)
{
    auto successor_generator = mimir::planners::create_sucessor_generator(problem, mimir::planners::SuccessorGeneratorType::LIFTED);
    return std::dynamic_pointer_cast<mimir::planners::LiftedSuccessorGenerator>(successor_generator);
}

std::shared_ptr<mimir::planners::GroundedSuccessorGenerator> create_grounded_successor_generator(const mimir::formalism::ProblemDescription& problem)
{
    auto successor_generator = mimir::planners::create_sucessor_generator(problem, mimir::planners::SuccessorGeneratorType::GROUNDED);
    return std::dynamic_pointer_cast<mimir::planners::GroundedSuccessorGenerator>(successor_generator);
}

void init_successor_generator(py::module& m)
{
    py::class_<mimir::planners::SuccessorGeneratorBase, mimir::planners::SuccessorGenerator> successor_generator_base(m, "SuccessorGenerator");
    py::class_<mimir::planners::LiftedSuccessorGenerator, std::shared_ptr<mimir::planners::LiftedSuccessorGenerator>> lifted_successor_generator(
        m,
        "LiftedSuccessorGenerator",
        successor_generator_base);
    py::class_<mimir::planners::GroundedSuccessorGenerator, std::shared_ptr<mimir::planners::GroundedSuccessorGenerator>> grounded_successor_generator(
        m,
        "GroundedSuccessorGenerator",
        successor_generator_base);

    successor_generator_base.def("get_applicable_actions",
                                 &mimir::planners::SuccessorGeneratorBase::get_applicable_actions,
                                 "state"_a,
                                 "Gets all ground actions applicable in the given state.");
    successor_generator_base.def("__repr__",
                                 [](const mimir::planners::SuccessorGeneratorBase& generator)
                                 { return "<SuccessorGenerator '" + generator.get_problem()->name + "'>"; });

    lifted_successor_generator.def(py::init(&create_lifted_successor_generator), "problem"_a);
    grounded_successor_generator.def(py::init(&create_grounded_successor_generator), "problem"_a);
}