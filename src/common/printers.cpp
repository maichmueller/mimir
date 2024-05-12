#include "mimir/common/printers.hpp"

#include "mimir/common/translations.hpp"
#include "mimir/formalism/factories.hpp"

namespace mimir
{

/// @brief Prints a vector to the output stream.
template<typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<const T*>& vec)
{
    os << "[";
    for (size_t i = 0; i < vec.size(); ++i)
    {
        if (i != 0)
        {
            os << ", ";
        }
        os << *vec[i];
    }
    os << "]";

    return os;
}

/// @brief Prints a State to the output stream.
std::ostream& operator<<(std::ostream& os, const std::tuple<DenseState, const PDDLFactories&>& data)
{
    const auto [state, pddl_factories] = data;
    auto out_ground_atoms = GroundAtomList {};
    to_ground_atoms(state, pddl_factories, out_ground_atoms);

    os << "State("
       << "state_id=" << state.get_id() << ", "
       << "ground_atoms=" << out_ground_atoms << ")";

    return os;
}

/// @brief Prints an Action to the output stream.
std::ostream& operator<<(std::ostream& os, const std::tuple<DenseAction, const PDDLFactories&>& data)
{
    const auto [action, pddl_factories] = data;

    auto positive_precondition_bitset = action.get_applicability_positive_precondition_bitset();
    auto negative_precondition_bitset = action.get_applicability_negative_precondition_bitset();
    auto positive_effect_bitset = action.get_unconditional_positive_effect_bitset();
    auto negative_effect_bitset = action.get_unconditional_negative_effect_bitset();
    auto positive_conditional_condition_bitsets = action.get_conditional_positive_precondition_bitsets();
    auto negative_conditional_condition_bitsets = action.get_conditional_negative_precondition_bitsets();
    auto conditional_effects = action.get_conditional_effects();

    auto positive_precondition = GroundAtomList {};
    auto negative_precondition = GroundAtomList {};
    auto positive_simple_effects = GroundAtomList {};
    auto negative_simple_effects = GroundAtomList {};
    auto positive_conditional_preconditions = std::vector<GroundAtomList> {};
    auto negative_conditional_preconditions = std::vector<GroundAtomList> {};

    to_ground_atoms(positive_precondition_bitset, pddl_factories, positive_precondition);
    to_ground_atoms(negative_precondition_bitset, pddl_factories, negative_precondition);
    to_ground_atoms(positive_effect_bitset, pddl_factories, positive_simple_effects);
    to_ground_atoms(negative_effect_bitset, pddl_factories, negative_simple_effects);

    const auto num_conditional_effects = action.get_conditional_effects().size();
    positive_conditional_preconditions.resize(num_conditional_effects);
    negative_conditional_preconditions.resize(num_conditional_effects);
    for (size_t i = 0; i < num_conditional_effects; ++i)
    {
        to_ground_atoms(positive_conditional_condition_bitsets[i], pddl_factories, positive_conditional_preconditions[i]);
        to_ground_atoms(negative_conditional_condition_bitsets[i], pddl_factories, negative_conditional_preconditions[i]);
    }

    os << "Action("
       << "name=" << action.get_action()->get_name() << ", "
       << "positive precondition=" << positive_precondition << ", "
       << "negative precondition=" << negative_precondition << ", "
       << "simple_delete=" << negative_simple_effects << ", "
       << "simple_add=" << positive_simple_effects << ", "
       << "conditional_effects=[";
    for (size_t i = 0; i < num_conditional_effects; ++i)
    {
        os << "[positive precondition=" << positive_conditional_preconditions[i] << ", "
           << "negative precondition=" << negative_conditional_preconditions[i] << ", "
           << "effect=" <<
            [&]()
        {
            return conditional_effects[i] < 0 ? "not " + pddl_factories.get_ground_atom(std::abs(conditional_effects[i] + 1))->str() :
                                                pddl_factories.get_ground_atom(conditional_effects[i])->str();
        }();
        os << "], ";
    }
    os << "])";

    return os;
}

std::ostream& operator<<(std::ostream& os, const std::tuple<DenseAxiom, const PDDLFactories&>& data)
{
    const auto [axiom, pddl_factories] = data;

    auto positive_precondition_bitset = axiom.get_applicability_positive_precondition_bitset();
    auto negative_precondition_bitset = axiom.get_applicability_negative_precondition_bitset();

    auto positive_precondition = GroundAtomList {};
    auto negative_precondition = GroundAtomList {};

    to_ground_atoms(positive_precondition_bitset, pddl_factories, positive_precondition);
    to_ground_atoms(negative_precondition_bitset, pddl_factories, negative_precondition);

    os << "Action("
       << "name=" << axiom.get_axiom()->get_literal()->get_atom()->get_predicate()->get_name() << ", "
       << "positive precondition=" << positive_precondition << ", "
       << "negative precondition=" << negative_precondition << ", "
       << "effect=" <<
        [&]()
    {
        return axiom.get_simple_effect() < 0 ? "not " + pddl_factories.get_ground_atom(std::abs(axiom.get_simple_effect() + 1))->str() :
                                               pddl_factories.get_ground_atom(axiom.get_simple_effect())->str();
    }();

    os << ")";

    return os;
}

std::ostream& operator<<(std::ostream& os, const DenseAction& action)
{
    os << "(" << action.get_action()->get_name();
    for (const auto& obj : action.get_objects())
    {
        os << " " << obj->get_name();
    }
    os << ")";
    return os;
}
}
