
#pragma once

#include "hashing.hpp"
#include "type_traits.hpp"
#include "deref.hpp"

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

template<typename... Ts>
struct overload : Ts...
{
    using Ts::operator()...;
};
template<typename... Ts>
overload(Ts...) -> overload<Ts...>;

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
auto as_range(Iter begin, Sent end)
{
    return RangeWrapper { begin, end };
}

template<typename RangeLike>
auto as_range(RangeLike& range)
{
    return RangeWrapper { std::ranges::begin(range), std::ranges::end(range) };
}

}
