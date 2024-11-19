#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "mimir/search/condition_grounders/conjunction_grounder.hpp"
#include "opaque_types.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;

void init_aag(py::module_& m)
{
    /* ConjunctionGrounder */

    class_<LiftedConjunctionGrounder, std::shared_ptr<LiftedConjunctionGrounder>>(m, "LiftedConjunctionGrounder")  //
        .def(py::init<Problem, VariableList, LiteralList<Static>, LiteralList<Fluent>, LiteralList<Derived>, std::shared_ptr<PDDLFactories>>(),
             py::arg("problem"),
             py::arg("variables"),
             py::arg("static_literals"),
             py::arg("fluent_literals"),
             py::arg("derived_literals"),
             py::arg("pddl_factories"))
        .def("ground", &LiftedConjunctionGrounder::ground, py::arg("state"));

    /* ApplicableActionGenerators */

    class_<IApplicableActionGenerator, std::shared_ptr<IApplicableActionGenerator>>(m, "IApplicableActionGenerator")  //
        .def(
            "compute_applicable_actions",
            [](IApplicableActionGenerator& self, State state)
            {
                auto applicable_actions = GroundActionList();
                self.generate_applicable_actions(state, applicable_actions);
                return applicable_actions;
            },
            py::keep_alive<0, 1>(),
            py::arg("state"))
        .def(
            "get_ground_actions",
            [](const IApplicableActionGenerator& self)
            {
                auto actions = self.get_ground_actions();
                return actions;
            },
            py::keep_alive<0, 1>())
        .def("get_ground_action", &IApplicableActionGenerator::get_ground_action, py::keep_alive<0, 1>(), py::arg("action_index"))
        .def(
            "get_ground_axioms",
            [](const IApplicableActionGenerator& self)
            {
                auto axioms = self.get_ground_axioms();
                return axioms;
            },
            py::keep_alive<0, 1>())
        .def("get_ground_action", &IApplicableActionGenerator::get_ground_axiom, py::keep_alive<0, 1>(), py::arg("axiom_index"))
        .def("get_problem", &IApplicableActionGenerator::get_problem, py::return_value_policy::reference_internal)
        .def("get_pddl_factories", &IApplicableActionGenerator::get_pddl_factories);

    // Lifted
    class_<DefaultLiftedApplicableActionGeneratorEventHandler,
           ILiftedApplicableActionGeneratorEventHandler,
           std::shared_ptr<DefaultLiftedApplicableActionGeneratorEventHandler>>(m, "DefaultLiftedApplicableActionGeneratorEventHandler")  //
        .def(py::init<>());

    class_<DebugLiftedApplicableActionGeneratorEventHandler,
           ILiftedApplicableActionGeneratorEventHandler,
           std::shared_ptr<DebugLiftedApplicableActionGeneratorEventHandler>>(m, "DebugLiftedApplicableActionGeneratorEventHandler")  //
        .def(py::init<>());

    class_<LiftedApplicableActionGenerator, IApplicableActionGenerator, std::shared_ptr<LiftedApplicableActionGenerator>>(m,
                                                                                                                          "LiftedApplicableActionGenerator")  //
        .def(py::init<Problem, std::shared_ptr<PDDLFactories>>(), py::arg("problem"), py::arg("pddl_factories"))
        .def(py::init<Problem, std::shared_ptr<PDDLFactories>, std::shared_ptr<ILiftedApplicableActionGeneratorEventHandler>>(),
             py::arg("problem"),
             py::arg("pddl_factories"),
             py::arg("event_handler"));

    // Grounded
    class_<DefaultGroundedApplicableActionGeneratorEventHandler,
           IGroundedApplicableActionGeneratorEventHandler,
           std::shared_ptr<DefaultGroundedApplicableActionGeneratorEventHandler>>(m, "DefaultGroundedApplicableActionGeneratorEventHandler")  //
        .def(py::init<>());

    class_<DebugGroundedApplicableActionGeneratorEventHandler,
           IGroundedApplicableActionGeneratorEventHandler,
           std::shared_ptr<DebugGroundedApplicableActionGeneratorEventHandler>>(m, "DebugGroundedApplicableActionGeneratorEventHandler")  //
        .def(py::init<>());

    class_<GroundedApplicableActionGenerator, IApplicableActionGenerator, std::shared_ptr<GroundedApplicableActionGenerator>>(
        m,
        "GroundedApplicableActionGenerator")  //
        .def(py::init<Problem, std::shared_ptr<PDDLFactories>>(), py::arg("problem"), py::arg("pddl_factories"))
        .def(py::init<Problem, std::shared_ptr<PDDLFactories>, std::shared_ptr<IGroundedApplicableActionGeneratorEventHandler>>(),
             py::arg("problem"),
             py::arg("pddl_factories"),
             py::arg("event_handler"));
}