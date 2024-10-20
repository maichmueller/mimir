
#ifndef MIMIR_UTILS_UTILS_HPP
#define MIMIR_UTILS_UTILS_HPP

#include <memory>
#include <optional>
#include <type_traits>

#ifndef FWD
#define FWD(x) std::forward<decltype(x)>(x)
#endif  // FWD

#ifndef AS_LAMBDA
#define AS_LAMBDA(func) [](auto&&... args) { return func(FWD(args)...); }
#endif  // AS_LAMBDA
/// creates a lambda wrapper around the given function and captures surrounding state by ref
#ifndef AS_CPTR_LAMBDA
#define AS_CPTR_LAMBDA(func) [&](auto&&... args) { return func(FWD(args)...); }
#endif  // AS_CPTR_LAMBDA

/// creates a lambda wrapper around the given function with perfect-return
/// (avoids copies on return when references are passed back (and supposed to remain references)
#ifndef AS_PRFCT_LAMBDA
#define AS_PRFCT_LAMBDA(func) [](auto&&... args) -> decltype(auto) { return func(FWD(args)...); }
#endif  // AS_PRFCT_LAMBDA

/// creates a lambda wrapper around the given function with perfect-return and captures surrounding
/// state by ref
#ifndef AS_PRFCT_CPTR_LAMBDA
#define AS_PRFCT_CPTR_LAMBDA(func) [&](auto&&... args) -> decltype(auto) { return func(FWD(args)...); }
#endif  // AS_PRFCT_CPTR_LAMBDA

namespace mimir
{

/// is_specialization checks whether T is a specialized template class of 'Template'
/// This has the limitation of
/// usage:
///     constexpr bool is_vector = is_specialization< std::vector< int >, std::vector>;
///
/// Note that this trait has 2 limitations:
///  1) Does not work with non-type parameters.
///     (i.e. templates such as std::array will not be compatible with this type trait)
///  2) Generally, template aliases do not get captured as the underlying template but as the
///     alias template. As such the following scenarios will return for an alias
///     template <typename T> using uptr = std::unique_ptr< T >:
///          specialization<uptr<int>, std::unique_ptr>;              -> true
///          specialization<uptr<int>, uptr> << std::endl;            -> false
///          specialization<std::unique_ptr<int>, std::unique_ptr>;   -> true
///          specialization<std::unique_ptr<int>, uptr>;              -> false
;
template<class T, template<class...> class Template>
struct is_specialization : std::false_type
{
};

template<template<class...> class Template, class... Args>
struct is_specialization<Template<Args...>, Template> : std::true_type
{
};

template<class T, template<class...> class Template>
constexpr bool is_specialization_v = is_specialization<T, Template>::value;

template<typename T>
using raw = std::remove_cvref<T>;

template<typename T>
using raw_t = typename raw<T>::type;

template<typename T>
decltype(auto) deref(T&& t)
{
    return FWD(t);
}

template<typename T>
    requires std::is_pointer_v<raw_t<T>>                               //
             or is_specialization_v<raw_t<T>, std::reference_wrapper>  //
             or is_specialization_v<raw_t<T>, std::optional>           //
             or is_specialization_v<raw_t<T>, std::shared_ptr>         //
             or is_specialization_v<raw_t<T>, std::unique_ptr>         //
             or std::input_or_output_iterator<raw_t<T>>                //
decltype(auto) deref(T&& t)
{
    if constexpr (std::is_pointer_v<raw_t<T>>                 //
                  or std::input_or_output_iterator<raw_t<T>>  //
                  or is_specialization_v<raw_t<T>, std::optional>)
    {
        return *FWD(t);
    }
    else
    {
        return deref(FWD(t).get());
    }
}

template<typename T>
using dereffed_t = decltype(deref(std::declval<T>()));

template<typename... Ts>
struct overload : Ts...
{
    using Ts::operator()...;
};
template < typename... Ts >
overload(Ts...) -> overload< Ts... >;


/// logical XOR of the conditions (using fold expressions and bitwise xor)
template<typename... Conditions>
struct logical_xor : std::integral_constant<bool, (Conditions::value ^ ...)>
{
};
/// helper variable to get the contained value of the trait
template<typename... Conditions>
constexpr bool logical_xor_v = logical_xor<Conditions...>::value;

/// logical AND of the conditions (merely aliased)
template<typename... Conditions>
using logical_and = std::conjunction<Conditions...>;
/// helper variable to get the contained value of the trait
template<typename... Conditions>
constexpr bool logical_and_v = logical_and<Conditions...>::value;

/// logical OR of the conditions (merely aliased)
template<typename... Conditions>
using logical_or = std::disjunction<Conditions...>;
/// helper variable to get the contained value of the trait
template<typename... Conditions>
constexpr bool logical_or_v = logical_or<Conditions...>::value;
/// check if type T matches any of the given types in Ts...

/// logical NEGATION of the conditions (specialized for booleans)
template<bool... conditions>
constexpr bool none_of = logical_and_v<std::integral_constant<bool, not conditions>...>;

/// logical ANY of the conditions (specialized for booleans)
template<bool... conditions>
constexpr bool any_of = logical_or_v<std::integral_constant<bool, conditions>...>;

/// logical AND of the conditions (specialized for booleans)
template<bool... conditions>
constexpr bool all_of = logical_and_v<std::integral_constant<bool, conditions>...>;

template<class T, class... Ts>
struct is_any : ::std::disjunction<::std::is_same<T, Ts>...>
{
};
template<class T, class... Ts>
inline constexpr bool is_any_v = is_any<T, Ts...>::value;

template<class T, class... Ts>
struct is_none : ::std::negation<is_any<T, Ts...>>
{
};
template<class T, class... Ts>
inline constexpr bool is_none_v = is_none<T, Ts...>::value;

template<class T, class... Ts>
struct all_same : ::std::conjunction<::std::is_same<T, Ts>...>
{
};
template<class T, class... Ts>
inline constexpr bool all_same_v = all_same<T, Ts...>::value;

template<typename Iter, typename Sent>
class RangeWrapper
{
public:
    RangeWrapper(Iter begin, Sent end) : m_begin(begin), m_end(end) {}

    auto begin() const { return m_begin; }
    auto end() const { return m_end; }

private:
    Iter m_begin;
    Sent m_end;
};

template<typename Iter, typename Sent>
RangeWrapper<Iter, Sent> as_range(Iter begin, Sent end)
{
    return RangeWrapper { begin, end };
}

}

#endif  // MIMIR_UTILS_UTILS_HPP
