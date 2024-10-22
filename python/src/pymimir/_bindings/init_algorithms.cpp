#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "opaque_types.hpp"
#include "trampolines.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;


void init_algorithms(py::module_& m)
{
    /* Algorithms */
    py::class_<IAlgorithm, std::shared_ptr<IAlgorithm>>(m, "IAlgorithm")  //
        .def("find_solution",
             [](IAlgorithm& algorithm)
             {
                 auto out_actions = GroundActionList {};
                 auto search_status = algorithm.find_solution(out_actions);
                 return std::make_tuple(search_status, out_actions);
             });

    // AStar
    py::class_<AStarAlgorithmStatistics>(m, "AStarAlgorithmStatistics")  //
        .def("get_num_generated", &AStarAlgorithmStatistics::get_num_generated)
        .def("get_num_expanded", &AStarAlgorithmStatistics::get_num_expanded)
        .def("get_num_deadends", &AStarAlgorithmStatistics::get_num_deadends)
        .def("get_num_pruned", &AStarAlgorithmStatistics::get_num_pruned)
        .def("get_num_generated_until_f_value", &AStarAlgorithmStatistics::get_num_generated_until_f_value)
        .def("get_num_expanded_until_f_value", &AStarAlgorithmStatistics::get_num_expanded_until_f_value)
        .def("get_num_deadends_until_f_value", &AStarAlgorithmStatistics::get_num_deadends_until_f_value)
        .def("get_num_pruned_until_f_value", &AStarAlgorithmStatistics::get_num_pruned_until_f_value);
    py::class_<IAStarAlgorithmEventHandler, std::shared_ptr<IAStarAlgorithmEventHandler>>(m,
                                                                                          "IAStarAlgorithmEventHandler")  //
        .def("get_statistics", &IAStarAlgorithmEventHandler::get_statistics);
    py::class_<DefaultAStarAlgorithmEventHandler, IAStarAlgorithmEventHandler, std::shared_ptr<DefaultAStarAlgorithmEventHandler>>(
        m,
        "DefaultAStarAlgorithmEventHandler")  //
        .def(py::init<bool>(), py::arg("quiet") = true);
    py::class_<DebugAStarAlgorithmEventHandler, IAStarAlgorithmEventHandler, std::shared_ptr<DebugAStarAlgorithmEventHandler>>(
        m,
        "DebugAStarAlgorithmEventHandler")  //
        .def(py::init<bool>(), py::arg("quiet") = true);
    py::class_<DynamicAStarAlgorithmEventHandlerBase,
               IAStarAlgorithmEventHandler,
               IPyDynamicAStarAlgorithmEventHandlerBase,
               std::shared_ptr<DynamicAStarAlgorithmEventHandlerBase>>(m,
                                                                       "AStarAlgorithmEventHandlerBase")  //
        .def(py::init<bool>(), py::arg("quiet") = true);
    py::class_<AStarAlgorithm, IAlgorithm, std::shared_ptr<AStarAlgorithm>>(m, "AStarAlgorithm")  //
        .def(py::init<std::shared_ptr<IApplicableActionGenerator>, std::shared_ptr<IHeuristic>>(), py::arg("applicable_action_generator"), py::arg("heuristic"))
        .def(py::init<std::shared_ptr<IApplicableActionGenerator>,
                      std::shared_ptr<StateRepository>,
                      std::shared_ptr<IHeuristic>,
                      std::shared_ptr<IAStarAlgorithmEventHandler>>(),
             py::arg("applicable_action_generator"),
             py::arg("state_repository"),
             py::arg("heuristic"),
             py::arg("event_handler"));

    // BrFS
    py::class_<BrFSAlgorithmStatistics>(m, "BrFSAlgorithmStatistics")  //
        .def("get_num_generated", &BrFSAlgorithmStatistics::get_num_generated)
        .def("get_num_expanded", &BrFSAlgorithmStatistics::get_num_expanded)
        .def("get_num_deadends", &BrFSAlgorithmStatistics::get_num_deadends)
        .def("get_num_pruned", &BrFSAlgorithmStatistics::get_num_pruned)
        .def("get_num_generated_until_g_value", &BrFSAlgorithmStatistics::get_num_generated_until_g_value)
        .def("get_num_expanded_until_g_value", &BrFSAlgorithmStatistics::get_num_expanded_until_g_value)
        .def("get_num_deadends_until_g_value", &BrFSAlgorithmStatistics::get_num_deadends_until_g_value)
        .def("get_num_pruned_until_g_value", &BrFSAlgorithmStatistics::get_num_pruned_until_g_value);
    py::class_<IBrFSAlgorithmEventHandler, std::shared_ptr<IBrFSAlgorithmEventHandler>>(m, "IBrFSAlgorithmEventHandler")
        .def("get_statistics", &IBrFSAlgorithmEventHandler::get_statistics);
    py::class_<DefaultBrFSAlgorithmEventHandler, IBrFSAlgorithmEventHandler, std::shared_ptr<DefaultBrFSAlgorithmEventHandler>>(
        m,
        "DefaultBrFSAlgorithmEventHandler")  //
        .def(py::init<>());
    py::class_<DebugBrFSAlgorithmEventHandler, IBrFSAlgorithmEventHandler, std::shared_ptr<DebugBrFSAlgorithmEventHandler>>(
        m,
        "DebugBrFSAlgorithmEventHandler")  //
        .def(py::init<>());
    py::class_<BrFSAlgorithm, IAlgorithm, std::shared_ptr<BrFSAlgorithm>>(m, "BrFSAlgorithm")
        .def(py::init<std::shared_ptr<IApplicableActionGenerator>>(), py::arg("applicable_action_generator"))
        .def(py::init<std::shared_ptr<IApplicableActionGenerator>, std::shared_ptr<StateRepository>, std::shared_ptr<IBrFSAlgorithmEventHandler>>(),
             py::arg("applicable_action_generator"),
             py::arg("state_repository"),
             py::arg("event_handler"));

    // IW
    py::class_<TupleIndexMapper, std::shared_ptr<TupleIndexMapper>>(m, "TupleIndexMapper")  //
        .def("to_tuple_index", &TupleIndexMapper::to_tuple_index, py::arg("atom_indices"))
        .def(
            "to_atom_indices",
            [](const TupleIndexMapper& self, const TupleIndex tuple_index)
            {
                auto atom_indices = AtomIndexList {};
                self.to_atom_indices(tuple_index, atom_indices);
                return atom_indices;
            },
            py::arg("tuple_index"))
        .def("tuple_index_to_string", &TupleIndexMapper::tuple_index_to_string, py::arg("tuple_index"))
        .def("get_num_atoms", &TupleIndexMapper::get_num_atoms)
        .def("get_arity", &TupleIndexMapper::get_arity)
        .def("get_factors", &TupleIndexMapper::get_factors, py::return_value_policy::reference_internal)
        .def("get_max_tuple_index", &TupleIndexMapper::get_max_tuple_index)
        .def("get_empty_tuple_index", &TupleIndexMapper::get_empty_tuple_index);

    py::class_<IWAlgorithmStatistics>(m, "IWAlgorithmStatistics")  //
        .def("get_effective_width", &IWAlgorithmStatistics::get_effective_width)
        .def("get_brfs_statistics_by_arity", &IWAlgorithmStatistics::get_brfs_statistics_by_arity);
    py::class_<IIWAlgorithmEventHandler, std::shared_ptr<IIWAlgorithmEventHandler>>(m, "IIWAlgorithmEventHandler")
        .def("get_statistics", &IIWAlgorithmEventHandler::get_statistics);
    py::class_<DefaultIWAlgorithmEventHandler, IIWAlgorithmEventHandler, std::shared_ptr<DefaultIWAlgorithmEventHandler>>(m, "DefaultIWAlgorithmEventHandler")
        .def(py::init<>());
    py::class_<IWAlgorithm, IAlgorithm, std::shared_ptr<IWAlgorithm>>(m, "IWAlgorithm")
        .def(py::init<std::shared_ptr<IApplicableActionGenerator>, size_t>(), py::arg("applicable_action_generator"), py::arg("max_arity"))
        .def(py::init<std::shared_ptr<IApplicableActionGenerator>,
                      size_t,
                      std::shared_ptr<StateRepository>,
                      std::shared_ptr<IBrFSAlgorithmEventHandler>,
                      std::shared_ptr<IIWAlgorithmEventHandler>>(),
             py::arg("applicable_action_generator"),
             py::arg("max_arity"),
             py::arg("state_repository"),
             py::arg("brfs_event_handler"),
             py::arg("iw_event_handler"));

    // SIW
    py::class_<SIWAlgorithmStatistics>(m, "SIWAlgorithmStatistics")  //
        .def("get_maximum_effective_width", &SIWAlgorithmStatistics::get_maximum_effective_width)
        .def("get_average_effective_width", &SIWAlgorithmStatistics::get_average_effective_width)
        .def("get_iw_statistics_by_subproblem", &SIWAlgorithmStatistics::get_iw_statistics_by_subproblem);
    py::class_<ISIWAlgorithmEventHandler, std::shared_ptr<ISIWAlgorithmEventHandler>>(m, "ISIWAlgorithmEventHandler")
        .def("get_statistics", &ISIWAlgorithmEventHandler::get_statistics);
    py::class_<DefaultSIWAlgorithmEventHandler, ISIWAlgorithmEventHandler, std::shared_ptr<DefaultSIWAlgorithmEventHandler>>(m,
                                                                                                                             "DefaultSIWAlgorithmEventHandler")
        .def(py::init<>());
    py::class_<SIWAlgorithm, IAlgorithm, std::shared_ptr<SIWAlgorithm>>(m, "SIWAlgorithm")
        .def(py::init<std::shared_ptr<IApplicableActionGenerator>, size_t>(), py::arg("applicable_action_generator"), py::arg("max_arity"))
        .def(py::init<std::shared_ptr<IApplicableActionGenerator>,
                      size_t,
                      std::shared_ptr<StateRepository>,
                      std::shared_ptr<IBrFSAlgorithmEventHandler>,
                      std::shared_ptr<IIWAlgorithmEventHandler>,
                      std::shared_ptr<ISIWAlgorithmEventHandler>>(),
             py::arg("applicable_action_generator"),
             py::arg("max_arity"),
             py::arg("state_repository"),
             py::arg("brfs_event_handler"),
             py::arg("iw_event_handler"),
             py::arg("siw_event_handler"));
}