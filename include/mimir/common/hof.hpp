#pragma once

#include "mimir/common/macros.hpp"
#include "mimir/common/type_traits.hpp"

namespace mimir::hof
{

template<typename T>
struct unpack : std::true_type
{
    using type = T;
};

template<typename T>
constexpr bool unpack_v = unpack<T>::value;

template<typename F, typename... Fs>
struct compose
{
    template<typename... Args>
    constexpr decltype(auto) operator()(Args&&... args)
    {
        using result_t = raw_t<std::invoke_result_t<compose<Fs...>, Args...>>;
        if constexpr ((is_specialization_v<result_t, std::tuple> or is_specialization_v<result_t, std::pair>) and unpack_v<F>)
        {
            return std::apply(F {}, compose<Fs...> {}(FWD(args)...));
        }
        else
        {
            return F {}(compose<Fs...> {}(FWD(args)...));
        }
    }
};

template<typename F>
struct compose<F>
{
    template<typename... Args>
    constexpr decltype(auto) operator()(Args&&... args)
    {
        return F {}(FWD(args)...);
    }
};

namespace detail
{
template<size_t, typename T>
using T_for_each_index = T;
}

template<size_t N>
struct project
{
    template<typename T>
    constexpr decltype(auto) operator()(T&& t) const noexcept(noexcept(std::get<N>(FWD(t))))
    {
        return std::get<N>(FWD(t));
    }
    template<typename T>
    constexpr auto operator()(auto&&... ts) const noexcept(noexcept(std::get<N>(FWD(ts)...)))
    {
        return std::forward_as_tuple(std::get<N>(FWD(ts))...);
    }
};

template<size_t N>
struct cista_project
{
    template<typename T>
    constexpr decltype(auto) operator()(T&& t) const noexcept(noexcept(cista::get<N>(FWD(t))))
    {
        return cista::get<N>(FWD(t));
    }
    template<typename T>
    constexpr auto operator()(auto&&... ts) const noexcept(noexcept(cista::get<N>(FWD(ts)...)))
    {
        return std::forward_as_tuple(cista::get<N>(FWD(ts))...);
    }
};

template<size_t N, size_t at = 0>
struct fork
{
    template<class T>
    constexpr auto operator()(T& ref) const noexcept
    {
        return as_tuple<T&>(ref, std::make_index_sequence<N> {});
    }

private:
    template<typename T, std::size_t... Is>
    constexpr auto as_tuple(T value, std::index_sequence<Is...>)
    {
        return std::tuple<detail::T_for_each_index<Is, T>...> { (static_cast<void>(Is), value)... };
    }
};

template<typename F>
struct map
{
    template<typename... Args>
    constexpr auto operator()(Args&&... args)
    {
        return std::tuple(F {}(FWD(args))...);
    }
};

template<typename... Fs>
struct zip_map
{
    template<typename... Args>
    constexpr auto operator()(Args&&... args)
    {
        return zip_apply_impl(std::make_index_sequence<sizeof...(Args)>(), std::forward_as_tuple(FWD(args)...));
    }

private:
    template<std::size_t... Is, typename... Args>
    constexpr auto zip_apply_impl(std::index_sequence<Is...>, Args&&... args)
    {
        return std::forward_as_tuple(Fs {}(project<Is> {}(FWD(args)))...);
    }
};

struct constructor
{
    template<typename T>
    constexpr auto operator()(auto&&... args) const noexcept(noexcept(T { FWD(args)... }))
    {
        return T { FWD(args)... };
    }

    template<template<typename...> class T, typename... Args>
    constexpr auto operator()(Args&&... args) const noexcept(noexcept(T<Args...> { FWD(args)... }))
    {
        return T<Args...> { FWD(args)... };
    }
};

template<size_t... Is>
struct join
{
    template<typename... Args>
    constexpr auto operator()(Args&&... args) const noexcept
    {
        // create a tuple by forwarding only the arguments at indices Is...
        return std::forward_as_tuple(project<Is>{}(std::forward_as_tuple(args...))...);
    };
};

}  // namespace mimir