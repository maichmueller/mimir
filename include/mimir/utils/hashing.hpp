#pragma once

#include <type_traits>

namespace mimir
{

template<typename T, typename Hasher = std::hash<std::remove_cvref_t<T>>>
struct ref_wrapper_hasher
{
    // allow for heterogenous lookup
    using is_transparent = std::true_type;

    auto operator()(const std::reference_wrapper<T>& value) const { return Hasher {}(value.get()); }
    auto operator()(const T& value) const { return Hasher {}(value); }
};

template<typename T>
    requires std::equality_comparable<T>
struct ref_wrapper_comparator
{
    // allow for heterogenous lookup
    using is_transparent = std::true_type;
    auto operator()(const std::reference_wrapper<T>& ref1, const std::reference_wrapper<T>& ref2) const { return ref1.get() == ref2.get(); }
    auto operator()(const std::reference_wrapper<T>& ref, const T& t) const { return ref.get() == t; }
    auto operator()(const T& t, const std::reference_wrapper<T>& ref) const { return t == ref.get(); }
    auto operator()(const T& t1, const T& t2) const { return t1 == t2; }
};

namespace detail
{

template<typename T, typename U>
concept pointer_like_to = requires(T t) {
    *t;
    requires std::same_as<U, std::remove_cvref_t<decltype(*t)>>;
};

}  // namespace detail

template<size_t N>
struct nth_proj
{
    template<typename T>
    constexpr decltype(auto) operator()(const T& t) const noexcept
    {
        return std::get<N>(t);
    }
};

template<size_t N>
struct nth_proj_hash
{
    template<typename T>
    constexpr decltype(auto) operator()(const T& t) const noexcept
    {
        return std::hash<std::tuple_element_t<N, T>> {}(std::get<N>(t));
    }
};

template<size_t N, typename FixedType = void>
struct nth_proj_comparator
{
    template<typename T, typename U>
    static constexpr bool convertible_to = std::convertible_to<T, U>;

    template<typename T, typename U = T>
        requires std::conditional_t<
            std::is_void_v<FixedType>,
            std::integral_constant<bool,
                                   convertible_to<std::tuple_element_t<N, U>, std::tuple_element_t<N, T>>
                                       or convertible_to<std::tuple_element_t<N, T>, std::tuple_element_t<N, U>>>,
            std::conditional_t<convertible_to<FixedType, std::tuple_element_t<N, T>> and convertible_to<FixedType, std::tuple_element_t<N, U>>,
                               std::true_type,
                               std::false_type>>::value
    constexpr decltype(auto) operator()(const T& t, const U& u) const noexcept
    {
        if constexpr (std::is_void_v<FixedType>)
        {
            return std::equal_to<std::conditional_t<convertible_to<std::tuple_element_t<N, T>, std::tuple_element_t<N, U>>,
                                                    std::tuple_element_t<N, T>,
                                                    std::tuple_element_t<N, U>>> {}(std::get<N>(t), std::get<N>(u));
        }
        else
        {
            return std::equal_to<FixedType> {}(std::get<N>(t), std::get<N>(u));
        }
    }

    template<typename T>
        requires std::conditional_t<std::is_void_v<FixedType>,
                                    std::true_type,
                                    std::conditional_t<convertible_to<FixedType, std::tuple_element_t<N, T>>, std::true_type, std::false_type>>::value
    constexpr decltype(auto) operator()(const T& t, const std::tuple_element_t<N, T>& u) const noexcept
    {
        if constexpr (std::is_void_v<FixedType>)
        {
            return std::equal_to<std::tuple_element_t<N, T>> {}(std::get<N>(t), u);
        }
        else
        {
            return std::equal_to<FixedType> {}(std::get<N>(t), u);
        }
    }
    template<typename T>
    constexpr decltype(auto) operator()(const std::tuple_element_t<N, T>& u, const T& t) const noexcept
    {
        return operator()(t, u);
    }
};

/// @brief class to hash the value of a pointer-like object (pointer, reference_wrapper, optional, shared_ptr, unique_ptr, etc...)
template<typename T, typename Hasher = std::hash<T>>
struct value_hasher
{
    // allow for heterogenous lookup
    using is_transparent = std::true_type;

    auto operator()(std::reference_wrapper<T> ref) const noexcept { return Hasher {}(ref); }
    auto operator()(std::reference_wrapper<const T> ref) const noexcept { return Hasher {}(ref); }
    auto operator()(const detail::pointer_like_to<T> auto& ptr_like) const noexcept { return Hasher {}(*ptr_like); }
    template<typename U>
    auto operator()(const U& u) const noexcept
    {
        return Hasher {}(u);
    }
};

/// @brief class which imports the operator() overload of each given type to call their std hash
template<template<typename> class ActualHasher, typename T, typename... Ts>
struct hasher : public hasher<ActualHasher, Ts...>, public hasher<ActualHasher, T>
{
    using hasher<ActualHasher, T>::operator();
    using hasher<ActualHasher, Ts...>::operator();
};

template<template<typename> class ActualHasher, typename T>
struct hasher<ActualHasher, T> : ActualHasher<T>
{
    using ActualHasher<T>::operator();
};

/// @brief class which imports the operator() overload of each given type to call their std hash
template<typename... Ts>
using std_hasher = hasher<std::hash, Ts...>;

template<template<class> class Hasher, typename T>
struct variant_hasher;

/// @brief A helper class to enable heterogenous hashing of variant types
///
/// Each individual type T is hashable by their own Hasher< T > class without prior conversion to
/// the variant type. However, this does necessiate a disambiguation for types that are implicitly
/// convertible to a T of the variant type. E.g. the call
///     variant_hasher< std::hash, std::variant< int, std::string >>{}("Char String")
/// would be ambiguous now and would have to be disambiguated by wrapping the character string in
/// std::string("Char String") or variant_type("Char String")
///
/// \tparam Hasher, the hash function (template functor) to be used
/// \tparam Ts, the variant types
template<template<class> class Hasher, typename... Ts>
struct variant_hasher<Hasher, std::variant<Ts...>> : public hasher<Hasher, Ts...>
{
    // allow for heterogenous lookup
    using is_transparent = std::true_type;

    // hashing of the actual variant type
    auto operator()(const std::variant<Ts...>& variant) const noexcept
    {
        return std::visit([]<typename VarType>(const VarType& var_element) noexcept { return Hasher<VarType> {}(var_element); }, variant);
    }
    // hashing of the individual variant types by their respective hash functions
    using hasher<Hasher, Ts...>::operator();
};

template<typename T, typename Hasher = std::hash<T>>
using variant_std_hasher = variant_hasher<std::hash, T>;


template<typename T, typename comparator = std::equal_to<T>>
    requires std::equality_comparable<T>
struct value_comparator
{
    // allow for heterogenous lookup
    using is_transparent = std::true_type;

    auto operator()(const detail::pointer_like_to<T> auto& ptr1, const detail::pointer_like_to<T> auto& ptr2) const noexcept
    {
        return comparator {}(*ptr1, *ptr2);
    }
    auto operator()(std::reference_wrapper<T> ref1, std::reference_wrapper<T>& ref2) const noexcept { return comparator {}(ref1.get(), ref2.get()); }
    auto operator()(const T& t1, const T& t2) const { return t1 == t2; }

    auto operator()(const detail::pointer_like_to<T> auto& ptr1, std::reference_wrapper<T> ref2) const noexcept { return comparator {}(*ptr1, ref2); }
    auto operator()(std::reference_wrapper<T> t1, const detail::pointer_like_to<T> auto& ptr2) const noexcept { return comparator {}(t1.get(), *ptr2); }

    auto operator()(const detail::pointer_like_to<T> auto& ptr1, const T& t2) const noexcept { return comparator {}(*ptr1, t2); }
    auto operator()(const T& t1, const detail::pointer_like_to<T> auto& ptr2) const noexcept { return comparator {}(t1, *ptr2); }

    auto operator()(std::reference_wrapper<T> ref1, const T& t2) const noexcept { return comparator {}(ref1.get(), t2); }
    auto operator()(const T& t1, std::reference_wrapper<T> ref2) const noexcept { return comparator {}(t1, ref2.get()); }

    /// forwarding template operator in case the comparator allows more inputs
    template<typename V, typename U>
    auto operator()(const V& v, const U& u) const noexcept
    {
        return comparator {}(v, u);
    }
};

/**
 * Literal class type that wraps a constant expression string.
 * Can be used as template parameter to differentiate via 'strings'
 */
template<size_t N>
struct StringLiteral
{
    constexpr StringLiteral(const char (&str)[N]) { std::copy_n(str, N, value); }

    char value[N];
};

template<typename... Ts>
struct Overload : Ts...
{
    using Ts::operator()...;
};
template<typename... Ts>
Overload(Ts...) -> Overload<Ts...>;

template<typename... Ts>
struct TransparentOverload : Ts...
{
    using is_transparent = std::true_type;
    using Ts::operator()...;
};
template<typename... Ts>
TransparentOverload(Ts...) -> TransparentOverload<Ts...>;

template<typename ReturnType>
struct monostate_error_visitor
{
    ReturnType operator()(std::monostate)
    {
        // this should never be visited, but if so --> error
        throw std::logic_error("We entered a std::monostate visit branch.");
        if constexpr (std::is_void_v<ReturnType>)
        {
            return;
        }
        else
        {
            return ReturnType {};
        }
    }
};

template<typename TargetVariant, typename... Ts>
auto make_overload_with_monostate(Ts... ts)
{
    return Overload {
        monostate_error_visitor<std::invoke_result_t<decltype([] { return std::visit<Overload<Ts...>, TargetVariant>; }), Overload<Ts...>, TargetVariant>> {},
        ts...
    };
}

}