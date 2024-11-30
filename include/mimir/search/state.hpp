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

#pragma once

#include "mimir/cista/containers/dynamic_bitset.h"
#include "mimir/cista/storage/unordered_set.h"
#include "mimir/common/concepts.hpp"
#include "mimir/common/macros.hpp"
#include "mimir/common/printers.hpp"
#include "mimir/common/types_cista.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/utils/utils.hpp"

#include <cista/containers/tuple.h>
#include <cista/serialization.h>
#include <ostream>
#include <range/v3/range/conversion.hpp>
#include <tuple>
#include <unordered_map>

namespace mimir
{

/// @brief `StateImpl` encapsulates the fluent and derived atoms of a planning state.
/// We refer to the fluent atoms as the non-extended state
/// and the fluent and derived atoms as the extended state.
struct StateImpl
{
    Index index = Index(0);
    FlatBitset fluent_atoms = FlatBitset();
    FlatBitset derived_atoms = FlatBitset();

    template<DynamicPredicateTag P>
    bool contains(GroundAtom<P> atom) const;

    bool contains(const AnyGroundAtom& atom) const;

    template<DynamicPredicateTag P>
    bool superset_of(const GroundAtomList<P>& atoms) const;

    template<std::ranges::range Range>
    bool superset_of(Range&& atoms) const
    {
        return std::ranges::all_of(atoms, AS_CPTR_LAMBDA(contains));
    }

    bool literal_holds(AnyGroundLiteral literal) const;

    template<DynamicPredicateTag P>
    bool literal_holds(GroundLiteral<P> literal) const;

    template<DynamicPredicateTag P>
    bool literals_hold(const GroundLiteralList<P>& literals) const;

    template<std::ranges::range Range>
    bool literals_hold(Range&& literals) const
    {
        return std::ranges::all_of(FWD(literals), AS_CPTR_LAMBDA(literal_holds));
    }

    template<std::ranges::range LiteralRange>
    auto get_unsatisfied_literals(LiteralRange&& literals) const
    {
        return literals | std::views::filter(std::not_fn(AS_CPTR_LAMBDA(literal_holds)));
    }

    template<std::ranges::range LiteralRange>
    auto get_satisfied_literals(LiteralRange&& literals) const
    {
        return literals | std::views::filter(AS_CPTR_LAMBDA(literal_holds));
    }

    /* Getters */

    Index& get_index();

    Index get_index() const;

    template<DynamicPredicateTag P>
    FlatBitset& get_atoms();

    template<DynamicPredicateTag P>
    const FlatBitset& get_atoms() const;

    bool operator==(const StateImpl& other) const;
};

}

// Only hash/compare the non-extended portion of a state, and the problem.
// The extended portion is always equal for the same non-extended portion.
// We use it for the unique state construction in the `StateRepository`.
template<>
struct cista::storage::DerefStdHasher<mimir::StateImpl>
{
    size_t operator()(const mimir::StateImpl* ptr) const;
};
template<>
struct cista::storage::DerefStdEqualTo<mimir::StateImpl>
{
    bool operator()(const mimir::StateImpl* lhs, const mimir::StateImpl* rhs) const;
};

namespace mimir
{

using StateImplSet = cista::storage::UnorderedSet<StateImpl>;

/**
 * Pretty printing
 */

template<>
std::ostream& operator<<(std::ostream& os, const std::tuple<Problem, State, const PDDLRepositories&>& data);
}

#include "mimir/common/macros.hpp"

#include <fmt/ostream.h>
FORMATTABLE(mimir::State);
FORMATTABLE(ARG(std::tuple<mimir::Problem, mimir::State, const mimir::PDDLRepositories&>));