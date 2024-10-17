#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;

using namespace mimir;

void init_state_space(py::module& m)
{
    // StateVertex
    py::class_<StateVertex>(m, "StateVertex")  //
        .def("__eq__", &StateVertex::operator==)
        .def("__hash__", [](const StateVertex& self) { return std::hash<StateVertex>()(self); })
        .def_property_readonly("index", &StateVertex::get_index)
        .def_property_readonly("state", [](const StateVertex& self) { return get_state(self); }, py::keep_alive<0, 1>());

    // GroundActionEdge
    py::class_<GroundActionEdge>(m, "GroundActionEdge")  //
        .def("__eq__", &GroundActionEdge::operator==)
        .def("__hash__", [](const GroundActionEdge& self) { return std::hash<GroundActionEdge>()(self); })
        .def_property_readonly("index", &GroundActionEdge::get_index)
        .def_property_readonly("source", &GroundActionEdge::get_source)
        .def_property_readonly("target", &GroundActionEdge::get_target)
        .def_property_readonly("cost", [](const GroundActionEdge& self) { return get_cost(self); })
        .def_property_readonly("creating_action", [](const GroundActionEdge& self) { return get_creating_action(self); }, py::keep_alive<0, 1>());

    // GroundActionsEdge
    py::class_<GroundActionsEdge>(m, "GroundActionsEdge")  //
        .def("__eq__", &GroundActionsEdge::operator==)
        .def("__hash__", [](const GroundActionsEdge& self) { return std::hash<GroundActionsEdge>()(self); })
        .def_property_readonly("index", &GroundActionsEdge::get_index)
        .def_property_readonly("source", &GroundActionsEdge::get_source)
        .def_property_readonly("target", &GroundActionsEdge::get_target)
        .def_property_readonly("cost", [](const GroundActionsEdge& self) { return get_cost(self); })
        .def_property_readonly(
            "actions",
            [](const GroundActionsEdge& self) { return GroundActionList(get_actions(self).begin(), get_actions(self).end()); },
            py::keep_alive<0, 1>())
        .def_property_readonly("representative_action", [](const GroundActionsEdge& self) { return get_representative_action(self); }, py::keep_alive<0, 1>());

    // StateSpace
    py::class_<StateSpaceOptions>(m, "StateSpaceOptions")
        .def(py::init<bool, bool, uint32_t, uint32_t>(),
             py::arg("use_unit_cost_one") = true,
             py::arg("remove_if_unsolvable") = true,
             py::arg("max_num_states") = std::numeric_limits<uint32_t>::max(),
             py::arg("timeout_ms") = std::numeric_limits<uint32_t>::max())
        .def_readwrite("use_unit_cost_one", &StateSpaceOptions::use_unit_cost_one)
        .def_readwrite("remove_if_unsolvable", &StateSpaceOptions::remove_if_unsolvable)
        .def_readwrite("max_num_states", &StateSpaceOptions::max_num_states)
        .def_readwrite("timeout_ms", &StateSpaceOptions::timeout_ms);

    py::class_<StateSpacesOptions>(m, "StateSpacesOptions")
        .def(py::init<StateSpaceOptions, bool, uint32_t>(),
             py::arg("state_space_options") = StateSpaceOptions(),
             py::arg("sort_ascending_by_num_states") = true,
             py::arg("num_threads") = std::thread::hardware_concurrency())
        .def_readwrite("state_space_options", &StateSpacesOptions::state_space_options)
        .def_readwrite("sort_ascending_by_num_states", &StateSpacesOptions::sort_ascending_by_num_states)
        .def_readwrite("num_threads", &StateSpacesOptions::num_threads);

    py::class_<StateSpace, std::shared_ptr<StateSpace>>(m, "StateSpace")  //
        .def("__str__",
             [](const StateSpace& self)
             {
                 std::stringstream ss;
                 ss << self;
                 return ss.str();
             })
        .def_static(
            "create",
            [](const std::string& domain_filepath, const std::string& problem_filepath, const StateSpaceOptions& options)
            { return StateSpace::create(domain_filepath, problem_filepath, options); },
            py::arg("domain_filepath"),
            py::arg("problem_filepaths"),
            py::arg("options") = StateSpaceOptions())
        .def_static(
            "create",
            [](Problem problem,
               std::shared_ptr<PDDLFactories> factories,
               std::shared_ptr<IApplicableActionGenerator> aag,
               std::shared_ptr<StateRepository> ssg,
               const StateSpaceOptions& options) { return StateSpace::create(problem, std::move(factories), std::move(aag), std::move(ssg), options); },
            py::arg("problem"),
            py::arg("factories"),
            py::arg("aag"),
            py::arg("ssg"),
            py::arg("options") = StateSpaceOptions())
        .def_static(
            "create",
            [](Problem problem, const std::shared_ptr<PDDLFactories>& factories, const StateSpaceOptions& options)
            { return StateSpace::create(problem, factories, options); },
            py::arg("problem"),
            py::arg("factories"),
            py::arg("options") = StateSpaceOptions())
        .def_static(
            "create",
            [](const std::string& domain_filepath, const std::vector<std::string>& problem_filepaths, const StateSpacesOptions& options)
            {
                auto problem_filepaths_ = std::vector<fs::path>(problem_filepaths.begin(), problem_filepaths.end());
                return StateSpace::create(domain_filepath, problem_filepaths_, options);
            },
            py::arg("domain_filepath"),
            py::arg("problem_filepaths"),
            py::arg("options") = StateSpacesOptions())
        .def_static(
            "create",
            [](const std::vector<
                   std::tuple<Problem, std::shared_ptr<PDDLFactories>, std::shared_ptr<IApplicableActionGenerator>, std::shared_ptr<StateRepository>>>&
                   memories,
               const StateSpacesOptions& options) { return StateSpace::create(memories, options); },
            py::arg("memories"),
            py::arg("options") = StateSpacesOptions())
        .def(
            "__iter__",
            [](const StateSpace& self) { return py::make_iterator(self.begin(), self.end()); },
            py::keep_alive<0, 1>())
        .def("compute_shortest_forward_distances_from_states", &StateSpace::compute_shortest_distances_from_states<ForwardTraversal>, py::arg("state_indices"))
        .def("compute_shortest_backward_distances_from_states",
             &StateSpace::compute_shortest_distances_from_states<BackwardTraversal>,
             py::arg("state_indices"))
        .def("compute_pairwise_shortest_forward_state_distances", &StateSpace::compute_pairwise_shortest_state_distances<ForwardTraversal>)
        .def("compute_pairwise_shortest_backward_state_distances", &StateSpace::compute_pairwise_shortest_state_distances<BackwardTraversal>)
        .def_property_readonly("problem", &StateSpace::get_problem, py::return_value_policy::reference_internal)
        .def_property_readonly("pddl_factories", &StateSpace::get_pddl_factories)
        .def_property_readonly("aag", &StateSpace::get_aag)
        .def_property_readonly("ssg", &StateSpace::get_ssg)
        .def("get_state", &StateSpace::get_state, py::arg("state_index"), py::return_value_policy::reference_internal)
        .def("get_index", &StateSpace::get_index, py::arg("state"))
        .def("get_initial_state_index", &StateSpace::get_initial_state_index)
        .def("get_initial_state", &StateSpace::get_initial_state, py::return_value_policy::reference_internal)
        .def("get_goal_state_indices", &StateSpace::get_goal_state_indices, py::return_value_policy::reference_internal)
        .def("get_goal_states",
             [](const StateSpace& self)
             {
                 auto view = self.get_goal_states_view();
                 return StateList(view.begin(), view.end());
             })
        .def("get_deadend_state_indices", &StateSpace::get_deadend_state_indices, py::return_value_policy::reference_internal)
        .def("get_deadend_states",
             [](const StateSpace& self)
             {
                 auto view = self.get_deadend_states_view();
                 return StateList(view.begin(), view.end());
             })
        .def(
            "get_forward_adjacent_states",
            [](const StateSpace& self, State state)
            {
                auto iterator = self.get_adjacent_states<ForwardTraversal>(state);
                return StateList(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state"))
        .def(
            "get_backward_adjacent_states",
            [](const StateSpace& self, State state)
            {
                auto view = self.get_adjacent_states<BackwardTraversal>(state);
                return StateList(view.begin(), view.end());
            },
            py::arg("state"))
        .def("get_num_states", &StateSpace::get_num_states)
        .def("get_num_goal_states", &StateSpace::get_num_goal_states)
        .def("get_num_deadend_states", &StateSpace::get_num_deadend_states)
        .def("is_goal_state", py::overload_cast<Index>(&StateSpace::is_goal_state, py::const_), py::arg("state"))
        .def("is_goal_state", py::overload_cast<State>(&StateSpace::is_goal_state, py::const_), py::arg("state"))
        .def("is_deadend_state", py::overload_cast<Index>(&StateSpace::is_deadend_state, py::const_), py::arg("state"))
        .def("is_deadend_state", py::overload_cast<State>(&StateSpace::is_deadend_state, py::const_), py::arg("state"))
        .def("is_alive_state", py::overload_cast<Index>(&StateSpace::is_alive_state, py::const_), py::arg("state"))
        .def("is_alive_state", py::overload_cast<State>(&StateSpace::is_alive_state, py::const_), py::arg("state"))
        .def("get_transitions", &StateSpace::get_transitions, py::return_value_policy::reference_internal)
        .def("get_transition_cost", &StateSpace::get_transition_cost, py::arg("transition_index"))
        .def(
            "get_forward_adjacent_transitions",
            [](const StateSpace& self, Index state)
            {
                auto iterator = self.get_adjacent_transitions<ForwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end(), py::keep_alive<0, 1>());
            },
            py::keep_alive<0, 1>(),
            py::arg("state"))
        .def(
            "get_forward_adjacent_transitions",
            [](const StateSpace& self, State state)
            {
                auto iterator = self.get_adjacent_transitions<ForwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end(), py::keep_alive<0, 1>());
            },
            py::keep_alive<0, 1>(),
            py::arg("state"))
        .def(
            "get_backward_adjacent_transitions",
            [](const StateSpace& self, State state)
            {
                auto iterator = self.get_adjacent_transitions<BackwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end(), py::keep_alive<0, 1>());
            },
            py::keep_alive<0, 1>(),
            py::arg("state"))
        .def(
            "get_backward_adjacent_transitions",
            [](const StateSpace& self, Index state)
            {
                auto iterator = self.get_adjacent_transitions<BackwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end(), py::keep_alive<0, 1>());
            },
            py::keep_alive<0, 1>(),
            py::arg("state"))
        .def("get_num_transitions", &StateSpace::get_num_transitions)
        .def("get_goal_distances", &StateSpace::get_goal_distances, py::return_value_policy::reference_internal)
        .def("get_max_goal_distance", &StateSpace::get_max_goal_distance)
        .def(
            "sample_state_with_goal_distance",
            [](const StateSpace& self, double goal_distance, uint64_t seed)
            {
                std::mt19937_64 rng { seed };
                self.sample_state_with_goal_distance(goal_distance, rng);
            },
            py::return_value_policy::reference_internal,
            py::arg("goal_distance"),
            py::arg("seed") = std::random_device {}());
}