/*
 * Copyright (C) 2023 Dominik Drexler and Simon Stahlberg
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef MIMIR_SEARCH_APPLICABLE_ACTION_GENERATORS_INTERFACE_HPP_
#define MIMIR_SEARCH_APPLICABLE_ACTION_GENERATORS_INTERFACE_HPP_

#include "mimir/formalism/formalism.hpp"
#include "mimir/search/actions.hpp"
#include "mimir/search/applicable_action_generators/tags.hpp"
#include "mimir/search/axioms.hpp"
#include "mimir/search/states.hpp"

namespace mimir
{

/**
 * Dynamic interface class.
 */
class IDynamicAAG
{
public:
    virtual ~IDynamicAAG() = default;

    /// @brief Generate all applicable actions for a given state.
    virtual void generate_applicable_actions(const State state, GroundActionList& out_applicable_actions) = 0;

    /// @brief Generate all applicable axioms for a given set of ground atoms by running fixed point computation.
    virtual void generate_and_apply_axioms(const FlatBitsetBuilder& fluent_state_atoms, FlatBitsetBuilder& ref_derived_state_atoms) = 0;

    // Notify that a new f-layer was reached
    virtual void on_finish_f_layer() = 0;

    /// @brief Notify that the search has finished
    virtual void on_end_search() = 0;

    /// @brief Return the action with the given id.
    [[nodiscard]] virtual GroundAction get_action(size_t action_id) const = 0;

    /* Getters */
    [[nodiscard]] virtual Problem get_problem() const = 0;
    [[nodiscard]] virtual PDDLFactories& get_pddl_factories() = 0;
    [[nodiscard]] virtual const PDDLFactories& get_pddl_factories() const = 0;
};

/**
 * Static interface class.
 */
template<typename Derived>
class IStaticAAG : public IDynamicAAG
{
private:
    IStaticAAG() = default;
    friend Derived;

    /// @brief Helper to cast to Derived.
    constexpr const auto& self() const { return static_cast<const Derived&>(*this); }
    constexpr auto& self() { return static_cast<Derived&>(*this); }

public:
    void generate_applicable_actions(const State state, GroundActionList& out_applicable_actions) override
    {
        self().generate_applicable_actions_impl(state, out_applicable_actions);
    }

    void generate_and_apply_axioms(const FlatBitsetBuilder& fluent_state_atoms, FlatBitsetBuilder& ref_derived_state_atoms) override
    {  //
        self().generate_and_apply_axioms_impl(fluent_state_atoms, ref_derived_state_atoms);
    }

    void on_finish_f_layer() override
    {  //
        self().on_finish_f_layer_impl();
    }

    void on_end_search() override
    {  //
        self().on_end_search_impl();
    }

    [[nodiscard]] Problem get_problem() const override
    {
        //
        return self().get_problem_impl();
    }

    [[nodiscard]] PDDLFactories& get_pddl_factories() override
    {  //
        return self().get_pddl_factories_impl();
    }

    [[nodiscard]] const PDDLFactories& get_pddl_factories() const override
    {  //
        return self().get_pddl_factories_impl();
    }
};

/**
 * General implementation class.
 *
 * Specialize it with your dispatcher.
 */
template<IsAAGDispatcher A>
class AAG : public IStaticAAG<AAG<A>>
{
};

}

#endif
