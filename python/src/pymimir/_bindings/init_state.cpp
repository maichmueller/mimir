#include "init_declarations.hpp"
#include "mimir/mimir.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
#include <range/v3/view/transform.hpp>
namespace py = pybind11;

using namespace mimir;
using namespace mimir::pymimir;

void init_StateImpl(py::module& m)
{
    py::class_<StateImpl>(m, "State")  //
        .def("__hash__", CONST_OVERLOAD(StateImpl::get_index))
        .def("__eq__", [](const StateImpl& lhs, const StateImpl& rhs) { return lhs == rhs; })
        .def("__repr__",
             [](StateImpl self)
             {
                 std::stringstream ss;
                 ss << "StateImpl(#Fluent=";
                 ss << std::to_string(self.get_atoms<Fluent>().count());
                 ss << "; #Derived=";
                 ss << std::to_string(self.get_atoms<Derived>().count());
                 ss << ")";
                 return ss.str();
             })
        .def("get_fluent_atom_indices",
             [](StateImpl self)
             {
                 auto atoms = self.get_atoms<Fluent>();
                 return std::vector<size_t>(atoms.begin(), atoms.end());
             })
        .def("get_derived_atom_indices",
             [](StateImpl self)
             {
                 auto atoms = self.get_atoms<Derived>();
                 return std::vector<size_t>(atoms.begin(), atoms.end());
             })
        .def("contains", &StateImpl::contains<Fluent>, py::arg("atom"))
        .def("contains", &StateImpl::contains<Derived>, py::arg("atom"))
        .def(
            "contains",
            [](const StateImpl& self, const AnyGroundAtom& atom) { return self.contains(atom); },
            py::arg("atom"))
        .def("superset_of", py::overload_cast<const GroundAtomList<Fluent>&>(&StateImpl::superset_of<Fluent>, py::const_), py::arg("atoms"))
        .def("superset_of", py::overload_cast<const GroundAtomList<Derived>&>(&StateImpl::superset_of<Derived>, py::const_), py::arg("atoms"))
        .def(
            "superset_of",
            [](const StateImpl& self, const py::iterable& rng)
            {
                auto range = as_range(rng.begin(), rng.end());
                return self.superset_of(range | std::views::transform(AS_LAMBDA(py::cast<AnyGroundAtom>)));
            },
            py::arg("atoms"))
        .def("literal_holds", py::overload_cast<GroundLiteral<Fluent>>(&StateImpl::literal_holds<Fluent>, py::const_), py::arg("literal"))
        .def("literal_holds", py::overload_cast<GroundLiteral<Derived>>(&StateImpl::literal_holds<Derived>, py::const_), py::arg("literal"))
        .def("literals_hold", py::overload_cast<const GroundLiteralList<Fluent>&>(&StateImpl::literals_hold<Fluent>, py::const_), py::arg("literals"))
        .def("literals_hold", py::overload_cast<const GroundLiteralList<Derived>&>(&StateImpl::literals_hold<Derived>, py::const_), py::arg("literals"))
        .def(
            "literals_hold",
            [](const StateImpl& self, const py::iterable& rng)
            {
                auto range = as_range(rng.begin(), rng.end());
                return self.literals_hold(std::views::transform(range, AS_LAMBDA(py::cast<AnyGroundLiteral>)));
            },
            py::arg("literals"))
        .def(
            "get_unsatisfied_literals",
            [](const StateImpl& self, Problem problem)
            {
                const auto& fluent_literals = problem->get_goal_condition<Fluent>();
                const auto& derived_literals = problem->get_goal_condition<Derived>();
                return insert_into_list(insert_into_list(self.get_unsatisfied_literals(fluent_literals)), self.get_unsatisfied_literals(derived_literals));
            },
            py::arg("problem"))
        .def(
            "get_unsatisfied_literals",
            [](const StateImpl& self, const py::iterable& iter)
            {
                auto rng = as_range(iter.begin(), iter.end());
                return self.get_unsatisfied_literals(std::views::transform(rng, AS_LAMBDA(py::cast<AnyGroundLiteral>)));
            },
            py::arg("iterable"))
        .def(
            "get_unsatisfied_literals",
            [](const StateImpl& self, const GroundLiteralList<Fluent>& literals)
            { return ranges::to<GroundLiteralList<Fluent>>(self.get_unsatisfied_literals(literals)); },
            py::arg("literals"))
        .def(
            "get_unsatisfied_literals",
            [](const StateImpl& self, const GroundLiteralList<Derived>& literals)
            { return ranges::to<GroundLiteralList<Derived>>(self.get_unsatisfied_literals(literals)); },
            py::arg("literals"))
        .def(
            "get_satisfied_literals",
            [](const StateImpl& self, Problem problem)
            {
                const auto& fluent_literals = problem->get_goal_condition<Fluent>();
                const auto& derived_literals = problem->get_goal_condition<Derived>();
                return insert_into_list(insert_into_list(self.get_satisfied_literals(fluent_literals)), self.get_satisfied_literals(derived_literals));
            },
            py::arg("problem"))
        .def(
            "get_satisfied_literals",
            [](const StateImpl& self, const py::iterable& iter)
            {
                auto rng = as_range(iter.begin(), iter.end());
                return self.get_satisfied_literals(std::views::transform(rng, AS_LAMBDA(py::cast<AnyGroundLiteral>)));
            },
            py::arg("iterable"))
        .def(
            "get_satisfied_literals",
            [](const StateImpl& self, const GroundLiteralList<Fluent>& literals)
            { return ranges::to<GroundLiteralList<Fluent>>(self.get_satisfied_literals(literals)); },
            py::arg("literals"))
        .def(
            "get_satisfied_literals",
            [](const StateImpl& self, const GroundLiteralList<Derived>& literals)
            { return ranges::to<GroundLiteralList<Derived>>(self.get_satisfied_literals(literals)); },
            py::arg("literals"))
        .def(
            "to_string",
            [](const StateImpl& self, Problem problem, const PDDLFactories& pddl_factories)
            {
                std::stringstream ss;
                ss << std::make_tuple(problem, &self, std::cref(pddl_factories));
                return ss.str();
            },
            py::arg("problem"),
            py::arg("pddl_factories"))
        .def("get_index", CONST_OVERLOAD(StateImpl::get_index));
    static_assert(!py::detail::vector_needs_copy<StateList>::value);  // Ensure return by reference + keep alive
    auto list_class = py::bind_vector<StateList>(m, "StateImplList");
    def_opaque_vector_repr<StateList>(list_class, "StateImplList");
    bind_const_span<std::span<const State>>(m, "StateImplSpan");
    bind_const_index_grouped_vector<IndexGroupedVector<const State>>(m, "IndexGroupedVector");

    /* StateImplRepository */
    py::class_<StateRepository, std::shared_ptr<StateRepository>>(m, "StateRepository")  //
        .def(py::init<std::shared_ptr<IApplicableActionGenerator>>(), py::arg("applicable_action_generator"))
        .def("get_or_create_initial_state", &StateRepository::get_or_create_initial_state, py::keep_alive<0, 1>())  // keep_alive because value type
        .def("get_or_create_state",
             &StateRepository::get_or_create_state,
             py::keep_alive<0, 1>(),
             py::arg("atoms"))  // keep_alive because value type
        .def("get_or_create_successor_state",
             &StateRepository::get_or_create_successor_state,
             py::keep_alive<0, 1>(),
             py::arg("state"),
             py::arg("action"))  // keep_alive because value type
        .def("get_state_count", &StateRepository::get_state_count)
        .def("get_reached_fluent_ground_atoms",
             [](const StateRepository& self)
             {
                 const auto& atoms = self.get_reached_fluent_ground_atoms();
                 return std::vector<size_t>(atoms.begin(), atoms.end());
             })
        .def("get_reached_derived_ground_atoms",
             [](const StateRepository& self)
             {
                 const auto& atoms = self.get_reached_derived_ground_atoms();
                 return std::vector<size_t>(atoms.begin(), atoms.end());
             });
}