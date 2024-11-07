#pragma once

#include "type_traits.hpp"

#include <type_traits>

namespace mimir
{

template<typename T>
decltype(auto) deref(T&& t)
{
    return FWD(t);
}

template<typename T, typename U = raw_t<T>>
    requires(std::same_as<raw_t<T>, U>                          //
                 and std::is_pointer_v<U>                       //
             or is_specialization_v<U, std::reference_wrapper>  //
             or is_specialization_v<U, std::optional>           //
             or is_specialization_v<U, std::shared_ptr>         //
             or is_specialization_v<U, std::unique_ptr>         //
             or std::input_or_output_iterator<U>)
decltype(auto) deref(T&& t)
{
    if constexpr (std::is_pointer_v<U>                 //
                  or std::input_or_output_iterator<U>  //
                  or is_specialization_v<U, std::optional>)
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

}