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
 *<
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once


#include "mimir/languages/description_logics/declarations.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <ranges>
#include <utility>
#include <variant>

namespace mimir::dl
{

/// @brief `UniqueDLHasher` is used to compare newly created dl constructors for uniqueness.
/// Since the children are unique, it suffices to create a combined hash from nested pointers.
template<typename T>
struct UniqueDLHasher
{
    size_t operator()(const T& element) const { return std::hash<T>()(element); }
};

struct UniqueDLHashCombiner
{
public:
    template<typename T>
    void operator()(size_t& seed, const T& value) const
    {
        seed ^= UniqueDLHasher<T>()(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }

    void operator()(size_t& seed, const std::size_t& value) const { seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2); }

    template<typename... Types>
    size_t operator()(const Types&... args) const
    {
        size_t seed = 0;
        ((*this)(seed, args), ...);
        return seed;
    }
};

/// Spezialization for std::ranges::forward_range.
template<typename ForwardRange>
    requires std::ranges::forward_range<ForwardRange>
struct UniqueDLHasher<ForwardRange>
{
    size_t operator()(const ForwardRange& range) const
    {
        std::size_t aggregated_hash = 0;
        for (const auto& item : range)
        {
            UniqueDLHashCombiner()(aggregated_hash, item);
        }
        return aggregated_hash;
    }
};

/// Spezialization for std::variant.
template<typename... Ts>
struct UniqueDLHasher<std::variant<Ts...>>
{
    size_t operator()(const std::variant<Ts...>& variant) const
    {
        return std::visit([](const auto& arg) { return UniqueDLHasher<decltype(arg)>()(arg); }, variant);
    }
};

/**
 * DL constructors
 */

/* Concepts */

template<PredicateCategory P>
struct UniqueDLHasher<const ConceptAtomicStateImpl<P>*>
{
    size_t operator()(const ConceptAtomicStateImpl<P>* e) const;
};

template<PredicateCategory P>
struct UniqueDLHasher<const ConceptAtomicGoalImpl<P>*>
{
    size_t operator()(const ConceptAtomicGoalImpl<P>* e) const;
};

template<>
struct UniqueDLHasher<const ConceptIntersectionImpl*>
{
    size_t operator()(const ConceptIntersectionImpl* e) const;
};

/* Roles */

template<PredicateCategory P>
struct UniqueDLHasher<const RoleAtomicStateImpl<P>*>
{
    size_t operator()(const RoleAtomicStateImpl<P>* e) const;
};

template<PredicateCategory P>
struct UniqueDLHasher<const RoleAtomicGoalImpl<P>*>
{
    size_t operator()(const RoleAtomicGoalImpl<P>* e) const;
};

template<>
struct UniqueDLHasher<const RoleIntersectionImpl*>
{
    size_t operator()(const RoleIntersectionImpl* e) const;
};

/**
 * DL grammar constructors
 */

template<IsConceptOrRole D>
struct UniqueDLHasher<const grammar::DerivationRuleImpl<D>*>
{
    size_t operator()(const grammar::DerivationRuleImpl<D>* e) const;
};

template<IsConceptOrRole D>
struct UniqueDLHasher<const grammar::NonTerminalImpl<D>*>
{
    size_t operator()(const grammar::NonTerminalImpl<D>* e) const;
};

template<IsConceptOrRole D>
struct UniqueDLHasher<const grammar::ConstructorOrNonTerminalChoice<D>*>
{
    size_t operator()(const grammar::ConstructorOrNonTerminalChoice<D>* e) const;
};

template<IsConceptOrRole D>
struct UniqueDLHasher<const grammar::ChoiceImpl<D>*>
{
    size_t operator()(const grammar::ChoiceImpl<D>* e) const;
};

/* Concepts */

template<PredicateCategory P>
struct UniqueDLHasher<const grammar::ConceptAtomicStateImpl<P>*>
{
    size_t operator()(const grammar::ConceptAtomicStateImpl<P>* e) const;
};

template<PredicateCategory P>
struct UniqueDLHasher<const grammar::ConceptAtomicGoalImpl<P>*>
{
    size_t operator()(const grammar::ConceptAtomicGoalImpl<P>* e) const;
};

template<>
struct UniqueDLHasher<const grammar::ConceptIntersectionImpl*>
{
    size_t operator()(const grammar::ConceptIntersectionImpl* e) const;
};

/* Roles */

template<PredicateCategory P>
struct UniqueDLHasher<const grammar::RoleAtomicStateImpl<P>*>
{
    size_t operator()(const grammar::RoleAtomicStateImpl<P>* e) const;
};

template<PredicateCategory P>
struct UniqueDLHasher<const grammar::RoleAtomicGoalImpl<P>*>
{
    size_t operator()(const grammar::RoleAtomicGoalImpl<P>* e) const;
};

template<>
struct UniqueDLHasher<const grammar::RoleIntersectionImpl*>
{
    size_t operator()(const grammar::RoleIntersectionImpl* e) const;
};

}


