#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "opaque_types.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
namespace py = pybind11;
using namespace mimir;

void init_abstraction(py::module& m)
{
    // FaithfulAbstraction
    class_<FaithfulAbstractionOptions>(m, "FaithfulAbstractionOptions")
        .def(py::init<bool, bool, bool, bool, uint32_t, uint32_t, uint32_t, ObjectGraphPruningStrategyEnum>(),
             py::arg("mark_true_goal_literals") = false,
             py::arg("use_unit_cost_one") = true,
             py::arg("remove_if_unsolvable") = true,
             py::arg("compute_complete_abstraction_mapping") = false,
             py::arg("max_num_concrete_states") = std::numeric_limits<uint32_t>::max(),
             py::arg("max_num_abstract_states") = std::numeric_limits<uint32_t>::max(),
             py::arg("timeout_ms") = std::numeric_limits<uint32_t>::max(),
             py::arg("pruning_strategy") = ObjectGraphPruningStrategyEnum::None)
        .def_readwrite("mark_true_goal_literals", &FaithfulAbstractionOptions::mark_true_goal_literals)
        .def_readwrite("use_unit_cost_one", &FaithfulAbstractionOptions::use_unit_cost_one)
        .def_readwrite("remove_if_unsolvable", &FaithfulAbstractionOptions::remove_if_unsolvable)
        .def_readwrite("compute_complete_abstraction_mapping", &FaithfulAbstractionOptions::compute_complete_abstraction_mapping)
        .def_readwrite("max_num_concrete_states", &FaithfulAbstractionOptions::max_num_concrete_states)
        .def_readwrite("max_num_abstract_states", &FaithfulAbstractionOptions::max_num_abstract_states)
        .def_readwrite("timeout_ms", &FaithfulAbstractionOptions::timeout_ms)
        .def_readwrite("pruning_strategy", &FaithfulAbstractionOptions::pruning_strategy);

    class_<FaithfulAbstractionsOptions>(m, "FaithfulAbstractionsOptions")
        .def(py::init<FaithfulAbstractionOptions, bool, uint32_t>(),
             py::arg("fa_options") = FaithfulAbstractionOptions(),
             py::arg("sort_ascending_by_num_states") = true,
             py::arg("num_threads") = std::thread::hardware_concurrency())
        .def_readwrite("fa_options", &FaithfulAbstractionsOptions::fa_options)
        .def_readwrite("sort_ascending_by_num_states", &FaithfulAbstractionsOptions::sort_ascending_by_num_states)
        .def_readwrite("num_threads", &FaithfulAbstractionsOptions::num_threads);

    class_<FaithfulAbstractStateVertex>(m, "FaithfulAbstractStateVertex")
        .def("get_index", &FaithfulAbstractStateVertex::get_index)
        .def(
            "get_state_vertices",
            [](const FaithfulAbstractStateVertex& self) { return std::vector<State>(get_states(self).begin(), get_states(self).end()); },
            py::keep_alive<0, 1>())
        .def(
            "get_representative_state",
            [](const FaithfulAbstractStateVertex& self) { return get_representative_state(self); },
            py::keep_alive<0, 1>())
        .def("get_certificate", [](const FaithfulAbstractStateVertex& self) { return get_certificate(self); }, py::return_value_policy::reference_internal);

    class_<FaithfulAbstraction, std::shared_ptr<FaithfulAbstraction>>(m, "FaithfulAbstraction")
        .def("__str__",
             [](const FaithfulAbstraction& self)
             {
                 std::stringstream ss;
                 ss << self;
                 return ss.str();
             })
        .def_static(
            "create",
            [](const std::string& domain_filepath, const std::string& problem_filepath, const FaithfulAbstractionOptions& options)
            { return FaithfulAbstraction::create(domain_filepath, problem_filepath, options); },
            py::arg("domain_filepath"),
            py::arg("problem_filepath"),
            py::arg("options") = FaithfulAbstractionOptions())
        .def_static(
            "create",
            [](Problem problem,
               std::shared_ptr<PDDLRepositories> factories,
               std::shared_ptr<IApplicableActionGenerator> aag,
               std::shared_ptr<StateRepository> ssg,
               const FaithfulAbstractionOptions& options) { return FaithfulAbstraction::create(problem, factories, aag, ssg, options); },
            py::arg("problem"),
            py::arg("factories"),
            py::arg("aag"),
            py::arg("ssg"),
            py::arg("options") = FaithfulAbstractionOptions())
        .def_static(
            "create",
            [](const std::string& domain_filepath, const std::vector<std::string>& problem_filepaths, const FaithfulAbstractionsOptions& options)
            {
                auto problem_filepaths_ = std::vector<fs::path>(problem_filepaths.begin(), problem_filepaths.end());
                return FaithfulAbstraction::create(domain_filepath, problem_filepaths_, options);
            },
            py::arg("domain_filepath"),
            py::arg("problem_filepaths"),
            py::arg("options") = FaithfulAbstractionsOptions())
        .def_static(
            "create",
            [](const std::vector<
                   std::tuple<Problem, std::shared_ptr<PDDLRepositories>, std::shared_ptr<IApplicableActionGenerator>, std::shared_ptr<StateRepository>>>&
                   memories,
               const FaithfulAbstractionsOptions& options) { return FaithfulAbstraction::create(memories, options); },
            py::arg("memories"),
            py::arg("options") = FaithfulAbstractionOptions())
        .def("compute_shortest_forward_distances_from_states",
             &FaithfulAbstraction::compute_shortest_distances_from_states<ForwardTraversal>,
             py::arg("state_indices"))
        .def("compute_shortest_backward_distances_from_states",
             &FaithfulAbstraction::compute_shortest_distances_from_states<BackwardTraversal>,
             py::arg("state_indices"))
        .def("compute_pairwise_shortest_forward_state_distances", &FaithfulAbstraction::compute_pairwise_shortest_state_distances<ForwardTraversal>)
        .def("compute_pairwise_shortest_backward_state_distances", &FaithfulAbstraction::compute_pairwise_shortest_state_distances<BackwardTraversal>)
        .def("get_problem", &FaithfulAbstraction::get_problem, py::return_value_policy::reference_internal)
        .def("get_pddl_repositories", &FaithfulAbstraction::get_pddl_repositories)
        .def("get_aag", &FaithfulAbstraction::get_aag)
        .def("get_ssg", &FaithfulAbstraction::get_ssg)
        .def("get_abstract_state_index", &FaithfulAbstraction::get_abstract_state_index, py::arg("state"))
        .def("get_state_vertices", &FaithfulAbstraction::get_states, py::return_value_policy::reference_internal)
        .def("get_concrete_to_abstract_state", &FaithfulAbstraction::get_concrete_to_abstract_state, py::return_value_policy::reference_internal)
        .def("get_initial_state_index", &FaithfulAbstraction::get_initial_state_index)
        .def("get_goal_state_indices", &FaithfulAbstraction::get_goal_state_indices, py::return_value_policy::reference_internal)
        .def("get_deadend_state_indices", &FaithfulAbstraction::get_deadend_state_indices, py::return_value_policy::reference_internal)
        .def(
            "get_forward_adjacent_states",
            [](const FaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_states<ForwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_backward_adjacent_states",
            [](const FaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_states<BackwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_forward_adjacent_state_indices",
            [](const FaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_state_indices<ForwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_backward_adjacent_state_indices",
            [](const FaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_state_indices<BackwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def("get_num_states", &FaithfulAbstraction::get_num_states)
        .def("get_num_goal_states", &FaithfulAbstraction::get_num_goal_states)
        .def("get_num_deadend_states", &FaithfulAbstraction::get_num_deadend_states)
        .def("is_goal_state", &FaithfulAbstraction::is_goal_state, py::arg("state_index"))
        .def("is_deadend_state", &FaithfulAbstraction::is_deadend_state, py::arg("state_index"))
        .def("is_alive_state", &FaithfulAbstraction::is_alive_state, py::arg("state_index"))
        .def("get_transitions", &FaithfulAbstraction::get_transitions, py::return_value_policy::reference_internal)
        .def("get_transition_cost", &FaithfulAbstraction::get_transition_cost, py::arg("transition_index"))
        .def(
            "get_forward_adjacent_transitions",
            [](const FaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_transitions<ForwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_backward_adjacent_transitions",
            [](const FaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_transitions<BackwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_forward_adjacent_transition_indices",
            [](const FaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_transition_indices<ForwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_backward_adjacent_transition_indices",
            [](const FaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_transition_indices<BackwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def("get_num_transitions", &FaithfulAbstraction::get_num_transitions)
        .def("get_goal_distances", &FaithfulAbstraction::get_goal_distances, py::return_value_policy::reference_internal);

    // GlobalFaithfulAbstraction

    class_<GlobalFaithfulAbstractState>(m, "GlobalFaithfulAbstractState")
        .def("__eq__", &GlobalFaithfulAbstractState::operator==)
        .def("__hash__", [](const GlobalFaithfulAbstractState& self) { return std::hash<GlobalFaithfulAbstractState>()(self); })
        .def("get_index", &GlobalFaithfulAbstractState::get_index)
        .def("get_global_index", &GlobalFaithfulAbstractState::get_global_index)
        .def("get_faithful_abstraction_index", &GlobalFaithfulAbstractState::get_faithful_abstraction_index)
        .def("get_faithful_abstract_state_index", &GlobalFaithfulAbstractState::get_faithful_abstract_state_index);

    class_<GlobalFaithfulAbstraction, std::shared_ptr<GlobalFaithfulAbstraction>>(m, "GlobalFaithfulAbstraction")
        .def("__str__",
             [](const GlobalFaithfulAbstraction& self)
             {
                 std::stringstream ss;
                 ss << self;
                 return ss.str();
             })
        .def_static(
            "create",
            [](const std::string& domain_filepath, const std::vector<std::string>& problem_filepaths, const FaithfulAbstractionsOptions& options)
            {
                auto problem_filepaths_ = std::vector<fs::path>(problem_filepaths.begin(), problem_filepaths.end());
                return GlobalFaithfulAbstraction::create(domain_filepath, problem_filepaths_, options);
            },
            py::arg("domain_filepath"),
            py::arg("problem_filepaths"),
            py::arg("options") = FaithfulAbstractionsOptions())
        .def_static(
            "create",
            [](const std::vector<
                   std::tuple<Problem, std::shared_ptr<PDDLRepositories>, std::shared_ptr<IApplicableActionGenerator>, std::shared_ptr<StateRepository>>>&
                   memories,
               const FaithfulAbstractionsOptions& options) { return GlobalFaithfulAbstraction::create(memories, options); },
            py::arg("memories"),
            py::arg("options") = FaithfulAbstractionsOptions())
        .def("compute_shortest_forward_distances_from_states",
             &GlobalFaithfulAbstraction::compute_shortest_distances_from_states<ForwardTraversal>,
             py::arg("state_indices"))
        .def("compute_shortest_backward_distances_from_states",
             &GlobalFaithfulAbstraction::compute_shortest_distances_from_states<BackwardTraversal>,
             py::arg("state_indices"))
        .def("compute_pairwise_shortest_forward_state_distances", &GlobalFaithfulAbstraction::compute_pairwise_shortest_state_distances<ForwardTraversal>)
        .def("compute_pairwise_shortest_backward_state_distances", &GlobalFaithfulAbstraction::compute_pairwise_shortest_state_distances<BackwardTraversal>)
        .def("get_index", &GlobalFaithfulAbstraction::get_index)
        .def("get_problem", &GlobalFaithfulAbstraction::get_problem, py::return_value_policy::reference_internal)
        .def("get_pddl_repositories", &GlobalFaithfulAbstraction::get_pddl_repositories)
        .def("get_aag", &GlobalFaithfulAbstraction::get_aag)
        .def("get_ssg", &GlobalFaithfulAbstraction::get_ssg)
        .def("get_abstractions", &GlobalFaithfulAbstraction::get_abstractions, py::return_value_policy::reference_internal)
        .def("get_abstract_state_index", py::overload_cast<State>(&GlobalFaithfulAbstraction::get_abstract_state_index, py::const_), py::arg("state"))
        .def("get_abstract_state_index", py::overload_cast<Index>(&GlobalFaithfulAbstraction::get_abstract_state_index, py::const_), py::arg("state_index"))
        .def("get_state_vertices", &GlobalFaithfulAbstraction::get_states, py::return_value_policy::reference_internal)
        .def("get_concrete_to_abstract_state", &GlobalFaithfulAbstraction::get_concrete_to_abstract_state, py::return_value_policy::reference_internal)
        .def("get_global_state_index_to_state_index",
             &GlobalFaithfulAbstraction::get_global_state_index_to_state_index,
             py::return_value_policy::reference_internal)
        .def("get_initial_state_index", &GlobalFaithfulAbstraction::get_initial_state_index)
        .def("get_goal_state_indices", &GlobalFaithfulAbstraction::get_goal_state_indices, py::return_value_policy::reference_internal)
        .def("get_deadend_state_indices", &GlobalFaithfulAbstraction::get_deadend_state_indices, py::return_value_policy::reference_internal)
        .def(
            "get_forward_adjacent_states",
            [](const GlobalFaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_states<ForwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_backward_adjacent_states",
            [](const GlobalFaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_states<BackwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_forward_adjacent_state_indices",
            [](const GlobalFaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_state_indices<ForwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_backward_adjacent_state_indices",
            [](const GlobalFaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_state_indices<BackwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def("get_num_states", &GlobalFaithfulAbstraction::get_num_states)
        .def("get_num_goal_states", &GlobalFaithfulAbstraction::get_num_goal_states)
        .def("get_num_deadend_states", &GlobalFaithfulAbstraction::get_num_deadend_states)
        .def("is_goal_state", &GlobalFaithfulAbstraction::is_goal_state, py::arg("state_index"))
        .def("is_deadend_state", &GlobalFaithfulAbstraction::is_deadend_state, py::arg("state_index"))
        .def("is_alive_state", &GlobalFaithfulAbstraction::is_alive_state, py::arg("state_index"))
        .def("get_num_isomorphic_states", &GlobalFaithfulAbstraction::get_num_isomorphic_states)
        .def("get_num_non_isomorphic_states", &GlobalFaithfulAbstraction::get_num_non_isomorphic_states)
        .def("get_transitions", &GlobalFaithfulAbstraction::get_transitions, py::return_value_policy::reference_internal)
        .def("get_transition_cost", &GlobalFaithfulAbstraction::get_transition_cost)
        .def(
            "get_forward_adjacent_transitions",
            [](const GlobalFaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_transitions<ForwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_backward_adjacent_transitions",
            [](const GlobalFaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_transitions<BackwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_forward_adjacent_transition_indices",
            [](const GlobalFaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_transition_indices<ForwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_backward_adjacent_transition_indices",
            [](const GlobalFaithfulAbstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_transition_indices<BackwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def("get_num_transitions", &GlobalFaithfulAbstraction::get_num_transitions)
        .def("get_goal_distances", &GlobalFaithfulAbstraction::get_goal_distances, py::return_value_policy::reference_internal);

    // Abstraction
    class_<Abstraction, std::shared_ptr<Abstraction>>(m, "Abstraction")  //
        .def(py::init<FaithfulAbstraction>(), py::arg("faithful_abstraction"))
        .def(py::init<GlobalFaithfulAbstraction>(), py::arg("global_faithful_abstraction"))
        .def("get_problem", &Abstraction::get_problem, py::return_value_policy::reference_internal)
        .def("get_pddl_repositories", &Abstraction::get_pddl_repositories)
        .def("get_aag", &Abstraction::get_aag)
        .def("get_ssg", &Abstraction::get_ssg)
        .def("get_abstract_state_index", &Abstraction::get_abstract_state_index)
        .def("get_initial_state_index", &Abstraction::get_initial_state_index)
        .def("get_goal_state_indices", &Abstraction::get_goal_state_indices, py::return_value_policy::reference_internal)
        .def("get_deadend_state_indices", &Abstraction::get_deadend_state_indices, py::return_value_policy::reference_internal)
        .def(
            "get_forward_adjacent_state_indices",
            [](const Abstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_state_indices<ForwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_backward_adjacent_state_indices",
            [](const Abstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_state_indices<BackwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def("get_num_states", &Abstraction::get_num_states)
        .def("get_num_goal_states", &Abstraction::get_num_goal_states)
        .def("get_num_deadend_states", &Abstraction::get_num_deadend_states)
        .def("is_goal_state", &Abstraction::is_goal_state, py::arg("state_index"))
        .def("is_deadend_state", &Abstraction::is_deadend_state, py::arg("state_index"))
        .def("is_alive_state", &Abstraction::is_alive_state, py::arg("state_index"))
        .def("get_transition_cost", &Abstraction::get_transition_cost, py::arg("transition_index"))
        .def(
            "get_forward_adjacent_transitions",
            [](const Abstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_transitions<ForwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_backward_adjacent_transitions",
            [](const Abstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_transitions<BackwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_forward_adjacent_transition_indices",
            [](const Abstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_transition_indices<ForwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def(
            "get_backward_adjacent_transition_indices",
            [](const Abstraction& self, Index state)
            {
                auto iterator = self.get_adjacent_transition_indices<BackwardTraversal>(state);
                return py::make_iterator(iterator.begin(), iterator.end());
            },
            py::keep_alive<0, 1>(),
            py::arg("state_index"))
        .def("get_num_transitions", &Abstraction::get_num_transitions)
        .def("get_goal_distances", &Abstraction::get_goal_distances, py::return_value_policy::reference_internal);
}