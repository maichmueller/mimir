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

#ifndef MIMIR_SEARCH_STATE_HPP_
#define MIMIR_SEARCH_STATE_HPP_

#include "cista/containers/dynamic_bitset.h"
#include "cista/containers/tuple.h"
#include "cista/serialization.h"
#include "cista/storage/unordered_set.h"
#include "mimir/common/concepts.hpp"
#include "mimir/common/types_cista.hpp"
#include "mimir/formalism/declarations.hpp"
#include "mimir/search/declarations.hpp"
#include "mimir/utils/utils.hpp"

#include <ostream>
#include <range/v3/range/conversion.hpp>
#include <tuple>
#include <unordered_map>

namespace mimir
{
using FlatState = cista::tuple<Index, FlatBitset, FlatBitset>;

/// @brief `StateBuilder` encapsulates mutable data of a state.
class StateBuilder
{
public:
    explicit StateBuilder();

    Index& get_index();

    template<DynamicPredicateCategory P>
    FlatBitset& get_atoms();

    FlatState& get_data();
    const FlatState& get_data() const;

private:
    FlatState m_data;
};

/// @brief `State` is an immutable view on the data of a state.
class State
{
public:
    explicit State(const FlatState& data);

    Index get_index() const;

    template<DynamicPredicateCategory P>
    bool contains(GroundAtom<P> atom) const;

    template<DynamicPredicateCategory P>
    bool superset_of(const GroundAtomList<P>& atoms) const;

    bool literal_holds(AnyGroundLiteral literal) const;

    template<typename LiteralT>
        requires is_any_v<LiteralT, GroundLiteral<Fluent>, GroundLiteral<Derived>, GroundLiteral<Static>>
    bool literal_holds(LiteralT literal) const;

    template<DynamicPredicateCategory P>
    bool literal_holds(GroundLiteral<P> literal) const;

    template<DynamicPredicateCategory P>
    bool literals_hold(const GroundLiteralList<P>& literals) const;

    template<std::ranges::range LiteralRange>
    auto get_unsatisfied_literals(LiteralRange&& literals) const
    {
        return literals | std::views::filter(std::not_fn(AS_CPTR_LAMBDA(literal_holds)));
    }

    template<std::ranges::range LiteralRange>
    auto get_satisfied_literals(LiteralRange&& literals) const
    {
        return FWD(literals) | std::views::filter(AS_CPTR_LAMBDA(literal_holds));
    }

    template<DynamicPredicateCategory P>
    const FlatBitset& get_atoms() const;

private:
    std::reference_wrapper<const FlatState> m_data;
};

// Compare the state index, since states returned by the `StateRepository` are already unique by their index.
extern bool operator==(State lhs, State rhs);
extern bool operator!=(State lhs, State rhs);

template<typename LiteralT>
    requires is_any_v<LiteralT, GroundLiteral<Fluent>, GroundLiteral<Derived>, GroundLiteral<Static>>
bool State::literal_holds(LiteralT literal) const
{
    if constexpr (std::is_same_v<LiteralT, GroundLiteral<Fluent>>)
    {
        return contains<Fluent>(literal);
    }
    else if constexpr (std::is_same_v<LiteralT, GroundLiteral<Derived>>)
    {
        return literal_holds<Derived>(literal);
    }
    else if constexpr (std::is_same_v<LiteralT, GroundLiteral<Static>>)
    {
        return true;
    }
    else
    {
        static_assert(dependent_false<LiteralT>::value, "Missing implementation for LiteralT.");
    }
}

}

// Only hash/compare the non-extended portion of a state, and the problem.
// The extended portion is always equal for the same non-extended portion.
// We use it for the unique state construction in the `StateRepository`.
template<>
struct cista::storage::DerefStdHasher<mimir::FlatState>
{
    size_t operator()(const mimir::FlatState* ptr) const;
};
template<>
struct cista::storage::DerefStdEqualTo<mimir::FlatState>
{
    bool operator()(const mimir::FlatState* lhs, const mimir::FlatState* rhs) const;
};

// Hash the state index, since states returned by the `StateRepository` are already unique by their index.
template<>
struct std::hash<mimir::State>
{
    size_t operator()(mimir::State element) const;
};

namespace mimir
{

using FlatStateSet = cista::storage::UnorderedSet<FlatState>;

using StateList = std::vector<State>;

template<typename T>
using StateMap = std::unordered_map<State, T>;
using StateSet = std::unordered_set<State>;

/**
 * Pretty printing
 */

extern std::ostream& operator<<(std::ostream& os, const std::tuple<Problem, State, const PDDLFactories&>& data);
}

#endif
