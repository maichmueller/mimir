#pragma once

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

#ifndef AS_STRUCT
#define AS_STRUCT(name, func)                                                         \
    struct name                                                                       \
    {                                                                                 \
        auto operator()(auto&&... args) const noexcept { return func(FWD(args)...); } \
    }
#endif  // AS_STRUCT

#ifndef AS_PRFCT_STRUCT
#define AS_PRFCT_STRUCT(name, func)                                                             \
    struct name                                                                                 \
    {                                                                                           \
        decltype(auto) operator()(auto&&... args) const noexcept { return func(FWD(args)...); } \
    }
#endif  // AS_STRUCT