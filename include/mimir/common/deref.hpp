#pragma once

#include "mimir/common/macros.hpp"
#include "mimir/common/type_traits.hpp"

#include <memory>
#include <optional>
#include <type_traits>

namespace mimir
{

template<typename T>
decltype(auto) deref(T&& t)
{
    return FWD(t);
}

template<typename T>
concept dereferencable = requires(T t) { *t; };

template<typename T>
    requires(is_specialization_v<raw_t<T>, std::reference_wrapper>  //
             or dereferencable<raw_t<T>>)
decltype(auto) deref(T&& t)
{
    if constexpr (dereferencable<raw_t<T>>)
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

AS_PRFCT_STRUCT(dereffer, deref);

}