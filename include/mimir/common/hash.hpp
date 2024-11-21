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

#include "deref.hpp"
#include "hof.hpp"
#include "mimir/common/concepts.hpp"
#include "mimir/utils/utils.hpp"
#include "type_traits.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace mimir
{
/**
 * Forward declarations
 */

template<typename T>
inline void hash_combine(size_t& seed, const T& value);

template<typename T, typename... Rest>
inline void hash_combine(size_t& seed, const Rest&... rest);

template<typename... Ts>
inline size_t hash_combine(const Ts&... rest);

/// @brief `Hash` defaults to `std::hash` but allows specializations inside the mimir namespace.
/// @tparam T
template<typename T>
struct Hash
{
    size_t operator()(const T& element) const { return ::std::hash<T>()(element); }
};

/// @brief Hash specialization for a forward range.
/// @tparam ForwardRange
template<::std::ranges::input_range R>
struct Hash<R>
{
    size_t operator()(const R& range) const
    {
        ::std::size_t aggregated_hash = 0;
        for (const auto& item : range)
        {
            mimir::hash_combine(aggregated_hash, item);
        }
        return aggregated_hash;
    }
};

/// @brief Hash specialization for a pair.
/// @tparam T1
/// @tparam T2
template<typename T1, typename T2>
struct Hash<::std::pair<T1, T2>>
{
    size_t operator()(const ::std::pair<T1, T2>& pair) const { return mimir::hash_combine(pair.first, pair.second); }
};

template<typename... Ts>
struct Hash<::std::tuple<Ts...>>
{
    size_t operator()(const ::std::tuple<Ts...>& tuple) const
    {
        size_t aggregated_hash = sizeof...(Ts);
        ::std::apply([&aggregated_hash](const Ts&... args) { (hash_combine(aggregated_hash, args), ...); }, tuple);
        return aggregated_hash;
    }
};

template<typename T>
inline void hash_combine(size_t& seed, const T& value)
{
    seed ^= Hash<T>()(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template<typename T, typename... Rest>
inline void hash_combine(size_t& seed, const Rest&... rest)
{
    (mimir::hash_combine(seed, rest), ...);
}

template<typename... Ts>
inline size_t hash_combine(const Ts&... rest)
{
    size_t seed = 0;
    (mimir::hash_combine(seed, rest), ...);
    return seed;
}

struct hash_combiner
{
    hash_combiner() = default;
    explicit hash_combiner(size_t seed_) : seed(seed_) {}

    size_t seed = 0;
    size_t operator()(auto&&... args) const noexcept { return mimir::hash_combine(seed, FWD(args)...); }
};

/// @brief class to hash the value of a pointer-like object (pointer, reference_wrapper, optional, shared_ptr, unique_ptr, etc...)
template<typename T, typename Hasher = Hash<raw_t<T>>>
using value_hasher = hof::compose<Hasher, dereffer>;

/// @brief class which imports the operator() overload of each given type to call the actual hasher
template<template<typename> class ActualHasher, typename T, typename... Ts>
struct multi_hasher : public multi_hasher<ActualHasher, Ts...>, public multi_hasher<ActualHasher, T>
{
    using multi_hasher<ActualHasher, T>::operator();
    using multi_hasher<ActualHasher, Ts...>::operator();
};

template<template<typename> class ActualHasher, typename T>
struct multi_hasher<ActualHasher, T> : ActualHasher<T>
{
    using ActualHasher<T>::operator();
};

/// @brief class which imports the operator() overload of each given type to call their ::std hash
template<typename... Ts>
using hasher = multi_hasher<Hash, Ts...>;

template<template<class> class Hasher, typename T>
struct variant_hasher;

/// @brief A helper class to enable heterogenous hashing of variant types
///
/// Each individual type T is hashable by their own Hasher< T > class without prior conversion to
/// the variant type. However, this does necessiate a disambiguation for types that are implicitly
/// convertible to a T of the variant type. E.g. the call
///     variant_hasher< ::std::hash, ::std::variant< int, ::std::string >>{}("Char String")
/// would be ambiguous now and would have to be disambiguated by wrapping the character string in
/// ::std::string("Char String") or variant_type("Char String")
///
/// \tparam Hasher, the hash function (template functor) to be used
/// \tparam Ts, the variant types
template<template<class> class Hasher, typename... Ts>
struct variant_hasher<Hasher, ::std::variant<Ts...>> : public multi_hasher<Hasher, Ts...>
{
    // allow for heterogenous lookup
    using is_transparent = ::std::true_type;

    // hashing of the actual variant type
    auto operator()(const ::std::variant<Ts...>& variant) const noexcept
    {
        return ::std::visit([]<typename VarType>(const VarType& var_element) noexcept { return Hasher<VarType> {}(var_element); }, variant);
    }
    // hashing of the individual variant types by their respective hash functions
    using multi_hasher<Hasher, Ts...>::operator();
};

template<typename T, typename Hasher = ::std::hash<T>>
using variant_std_hasher = variant_hasher<::std::hash, T>;

// Struct for transparent hashing
struct StringHashTransparent
{
    using is_transparent = ::std::true_type;  // Enable heterogeneous lookup

    ::std::size_t operator()(const ::std::string& key) const { return ::std::hash<::std::string> {}(key); }

    ::std::size_t operator()(const char* key) const { return ::std::hash<::std::string_view> {}(key); }

    ::std::size_t operator()(::std::string_view key) const { return ::std::hash<::std::string_view> {}(key); }
};

// Struct for transparent equality comparison
struct StringEqualTransparent
{
    using is_transparent = ::std::true_type;  // Enable heterogeneous lookup

    bool operator()(const ::std::string& lhs, const ::std::string& rhs) const { return lhs == rhs; }

    bool operator()(const ::std::string& lhs, ::std::string_view rhs) const { return lhs == rhs; }

    bool operator()(::std::string_view lhs, const ::std::string& rhs) const { return lhs == rhs; }

    bool operator()(::std::string_view lhs, ::std::string_view rhs) const { return lhs == rhs; }

    bool operator()(const char* lhs, const ::std::string& rhs) const { return ::std::string_view(lhs) == rhs; }

    bool operator()(const char* lhs, ::std::string_view rhs) const { return ::std::string_view(lhs) == rhs; }

    bool operator()(const ::std::string& lhs, const char* rhs) const { return lhs == ::std::string_view(rhs); }

    bool operator()(::std::string_view lhs, const char* rhs) const { return lhs == ::std::string_view(rhs); }

    bool operator()(const char* lhs, const char* rhs) const { return ::std::string_view(lhs) == ::std::string_view(rhs); }
};

}  // namespace mimir
