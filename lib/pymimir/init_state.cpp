
#include "init.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/generators/goal_matcher.hpp"
#include "mimir/generators/lifted_successor_generator.hpp"

#include <mimir/generators/literal_grounder.hpp>

using namespace py::literals;


bool state_matches_literals(const mimir::formalism::State& state, const mimir::formalism::LiteralList& literals)
{
    return mimir::formalism::literals_hold(literals, state);
}

void init_state(py::module& m)
{
    py::class_<mimir::formalism::StateImpl, mimir::formalism::State> state(m, "State");

    state.def("get_atoms", &mimir::formalism::StateImpl::get_atoms, "Gets the atoms of the state.");
    state.def("get_static_atoms", &mimir::formalism::StateImpl::get_static_atoms, "Gets the static atoms of the state.");
    state.def("get_fluent_atoms", &mimir::formalism::StateImpl::get_dynamic_atoms, "Gets the fluent atoms of the state.");
    state.def("get_problem", &mimir::formalism::StateImpl::get_problem, "Gets the problem associated with the state.");
    state.def("get_atoms_by_predicate",
              &mimir::formalism::StateImpl::get_atoms_grouped_by_predicate,
              "Gets a dictionary mapping predicates to ground atoms of the given predicate that are true in the state.");
    state.def("pack_object_ids_by_predicate_id",
              &mimir::formalism::StateImpl::pack_object_ids_by_predicate_id,
              "include_types"_a,
              "include_goal"_a,
              "Gets a dictionary mapping predicate identifiers to a flattened list of object ids, implicitly denoting the atoms true in the state, and a "
              "dictionary mapping identifiers to names.");
    state.def("literals_hold", &state_matches_literals, "literals"_a, "Tests whether all ground literals hold with respect to their polarity in the state.");
    state.def("__eq__", &mimir::formalism::StateImpl::operator==);
    state.def("__hash__", &mimir::formalism::StateImpl::hash);
    state.def("__repr__", [](const mimir::formalism::StateImpl& state) { return "<State '" + std::to_string(state.hash()) + "'>"; });

    state.def(
        "matches_all",
        [](const mimir::formalism::State& state, const mimir::formalism::AtomList& atom_list)
        {
            for (const auto& atom : atom_list)
            {
                if (!mimir::formalism::matches_any_in_state(atom, state))
                {
                    return false;
                }
            }

            return true;
        },
        "atom_list"_a,
        "Tests whether all atoms matches an atom in the state.");

    state.def(
        "matching_bindings",
        [](const mimir::formalism::State& state, const mimir::formalism::AtomList& atom_list)
        {
            mimir::planners::LiteralGrounder grounder(state->get_problem(), atom_list);
            return grounder.ground(state);
        },
        "atom_list"_a,
        "Gets all instantiations (and bindings) of the atom list that matches the state.");
}