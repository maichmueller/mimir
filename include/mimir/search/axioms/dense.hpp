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

#ifndef MIMIR_SEARCH_AXIOMS_DENSE_HPP_
#define MIMIR_SEARCH_AXIOMS_DENSE_HPP_

#include "mimir/formalism/factories.hpp"
#include "mimir/search/actions/dense.hpp"
#include "mimir/search/axioms/interface.hpp"
#include "mimir/search/flat_types.hpp"
#include "mimir/search/states.hpp"

#include <cstdint>
#include <ostream>
#include <tuple>

namespace mimir
{

/**
 * Flatmemory types
 */

struct FlatDerivedEffect
{
    bool is_negated;
    size_t atom_id;

    bool operator==(const FlatDerivedEffect& other) const
    {
        if (this != &other)
        {
            return is_negated == other.is_negated && atom_id == other.atom_id;
        }
        return true;
    }
};

using FlatDenseAxiomLayout = flatmemory::Tuple<uint32_t,  //
                                               Axiom,
                                               FlatObjectListLayout,
                                               FlatDenseStripsActionPreconditionLayout,
                                               FlatDenseStripsActionEffectLayout,
                                               FlatDerivedEffect>;
using FlatDenseAxiomBuilder = flatmemory::Builder<FlatDenseAxiomLayout>;
using FlatDenseAxiom = flatmemory::ConstView<FlatDenseAxiomLayout>;
using FlatDenseAxiomVector = flatmemory::VariableSizedTypeVector<FlatDenseAxiomLayout>;

struct FlatDenseAxiomHash
{
    size_t operator()(const FlatDenseAxiom& view) const
    {
        const auto axiom = view.get<1>();
        const auto objects = view.get<2>();
        return loki::hash_combine(axiom, objects.hash());
    }
};

struct FlatDenseAxiomEqual
{
    bool operator()(const FlatDenseAxiom& view_left, const FlatDenseAxiom& view_right) const
    {
        const auto axiom_left = view_left.get<1>();
        const auto objects_left = view_left.get<2>();
        const auto axiom_right = view_right.get<1>();
        const auto objects_right = view_right.get<2>();
        return (axiom_left == axiom_right) && (objects_left == objects_right);
    }
};

using FlatDenseAxiomSet = flatmemory::UnorderedSet<FlatDenseAxiomLayout, FlatDenseAxiomHash, FlatDenseAxiomEqual>;

/**
 * Implementation class
 */
template<>
class Builder<AxiomDispatcher<DenseStateTag>> :
    public IBuilder<Builder<AxiomDispatcher<DenseStateTag>>>,
    public IAxiomBuilder<Builder<AxiomDispatcher<DenseStateTag>>>
{
private:
    FlatDenseAxiomBuilder m_builder;

    /* Implement IBuilder interface */
    friend class IBuilder<Builder<AxiomDispatcher<DenseStateTag>>>;

    [[nodiscard]] FlatDenseAxiomBuilder& get_flatmemory_builder_impl() { return m_builder; }
    [[nodiscard]] const FlatDenseAxiomBuilder& get_flatmemory_builder_impl() const { return m_builder; }

    /* Implement IAxiomBuilder interface */
    friend class IAxiomBuilder<Builder<AxiomDispatcher<DenseStateTag>>>;

    [[nodiscard]] uint32_t& get_id_impl() { return m_builder.get<0>(); }
    [[nodiscard]] Axiom& get_axiom_impl() { return m_builder.get<1>(); }
    [[nodiscard]] FlatObjectListBuilder& get_objects_impl() { return m_builder.get<2>(); }

public:
    /* STRIPS part */
    [[nodiscard]] FlatDenseStripsActionPreconditionBuilder& get_strips_precondition() { return m_builder.get<3>(); }
    [[nodiscard]] FlatDenseStripsActionEffectBuilder& get_strips_effect() { return m_builder.get<4>(); }
    /* Simple effect */
    [[nodiscard]] FlatDerivedEffect& get_derived_effect() { return m_builder.get<5>(); }
};

/**
 * Implementation class
 *
 * Reads the memory layout generated by the search node builder.
 */
template<>
class ConstView<AxiomDispatcher<DenseStateTag>> :
    public IConstView<ConstView<AxiomDispatcher<DenseStateTag>>>,
    public IAxiomView<ConstView<AxiomDispatcher<DenseStateTag>>>
{
private:
    using DenseState = ConstView<StateDispatcher<DenseStateTag>>;

    FlatDenseAxiom m_view;

    /* Implement IView interface: */
    friend class IConstView<ConstView<AxiomDispatcher<DenseStateTag>>>;

    /// @brief Compute equality based on the lifted action and the objects assigned to the parameters.
    [[nodiscard]] bool are_equal_impl(const ConstView& other) const { return get_axiom() == other.get_axiom() && get_objects() == other.get_objects(); }

    /// @brief Compute hash based on the lifted action and the objects assigned to the parameters.
    [[nodiscard]] size_t hash_impl() const { return loki::hash_combine(get_axiom(), get_objects().hash()); }

    /* Implement IAxiomView interface */
    friend class IAxiomView<ConstView<AxiomDispatcher<DenseStateTag>>>;

    [[nodiscard]] uint32_t get_id_impl() const { return m_view.get<0>(); }
    [[nodiscard]] Axiom get_axiom_impl() const { return m_view.get<1>(); }
    [[nodiscard]] FlatObjectList get_objects_impl() const { return m_view.get<2>(); }

public:
    /// @brief Create a view on a Axiom.
    explicit ConstView(FlatDenseAxiom view) : m_view(view) {}

    /* STRIPS part */
    [[nodiscard]] FlatDenseStripsActionPrecondition get_strips_precondition() const { return m_view.get<3>(); }
    [[nodiscard]] FlatDenseStripsActionEffect get_strips_effect() const { return m_view.get<4>(); }
    /* Effect*/
    [[nodiscard]] FlatDerivedEffect get_derived_effect() const { return m_view.get<5>(); }

    template<flatmemory::IsBitset Bitset1, flatmemory::IsBitset Bitset2>
    [[nodiscard]] bool is_applicable(const Bitset1 state_fluent_atoms, const Bitset1 state_derived_atoms, const Bitset2 static_positive_bitset) const
    {  //
        return DenseStripsActionPrecondition(get_strips_precondition()).is_applicable(state_fluent_atoms, state_derived_atoms, static_positive_bitset);
    }

    template<flatmemory::IsBitset Bitset>
    [[nodiscard]] bool is_statically_applicable(const Bitset static_positive_bitset) const
    {  //
        return DenseStripsActionPrecondition(get_strips_precondition()).is_statically_applicable(static_positive_bitset);
    }
};

/**
 * Mimir types
 */

using DenseGroundAxiomBuilder = Builder<AxiomDispatcher<DenseStateTag>>;
using DenseGroundAxiom = ConstView<AxiomDispatcher<DenseStateTag>>;
using DenseGroundAxiomList = std::vector<DenseGroundAxiom>;
using DenseGroundAxiomSet = std::unordered_set<DenseGroundAxiom, loki::Hash<DenseGroundAxiom>, loki::EqualTo<DenseGroundAxiom>>;

/**
 * Translations
 */

extern DenseGroundAxiomList to_ground_axioms(const FlatDenseAxiomSet& flat_axioms);

/**
 * Pretty printing
 */

extern std::ostream& operator<<(std::ostream& os, const std::tuple<FlatDerivedEffect, const PDDLFactories&>& data);

extern std::ostream& operator<<(std::ostream& os, const std::tuple<DenseGroundAxiom, const PDDLFactories&>& data);
}

#endif
