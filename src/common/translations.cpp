#include "mimir/common/translations.hpp"

#include "mimir/formalism/effects.hpp"
#include "mimir/formalism/factories.hpp"
#include "mimir/formalism/ground_atom.hpp"

namespace mimir
{

void to_literals(Effect effect, LiteralList& ref_literals)
{
    if (const auto* effect_literal = std::get_if<EffectLiteralImpl>(effect))
    {
        ref_literals.emplace_back(effect_literal->get_literal());
    }
    else if (const auto* effect_and = std::get_if<EffectAndImpl>(effect))
    {
        for (const auto& inner_effect : effect_and->get_effects())
        {
            to_literals(inner_effect, ref_literals);
        }
    }
    else
    {
        throw std::runtime_error("only conjunctions are supported");
    }
}

void to_ground_atoms(const ConstBitsetView& bitset, const PDDLFactories& pddl_factories, GroundAtomList& out_ground_atoms)
{
    out_ground_atoms.clear();

    for (const auto& atom_id : bitset)
    {
        out_ground_atoms.emplace_back(pddl_factories.get_ground_atom(atom_id));
    }
}

void to_ground_atoms(ConstView<StateDispatcher<DenseStateTag>> state, const PDDLFactories& pddl_factories, GroundAtomList& out_ground_atoms)
{
    const auto& bitset = state.get_atoms_bitset();
    to_ground_atoms(bitset, pddl_factories, out_ground_atoms);
}

Plan to_plan(const std::vector<ConstView<ActionDispatcher<StateReprTag>>>& action_view_list)
{
    std::vector<std::string> actions;
    auto cost = 0;
    for (const auto action : action_view_list)
    {
        std::stringstream ss;
        ss << action;
        actions.push_back(ss.str());
        cost += action.get_cost();
    }
    return Plan(std::move(actions), cost);
}

}
