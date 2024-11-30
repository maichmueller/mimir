
#pragma once

#include "mimir/formalism/declarations.hpp"

#include <memory>
#include <optional>
#include <type_traits>

namespace mimir
{

template<typename... Ts>
struct overload : Ts...
{
    using Ts::operator()...;
};
template<typename... Ts>
overload(Ts...) -> overload<Ts...>;

template<typename... Ts>
struct transparent_overload : Ts...
{
    using is_transparent = std::true_type;
    using Ts::operator()...;
};
template<typename... Ts>
transparent_overload(Ts...) -> transparent_overload<Ts...>;

template<typename Iter, typename Sent>
auto make_range(Iter begin, Sent end)
{
    return std::ranges::subrange(begin, end);
}

template<typename Iter, typename Sent>
auto make_range(Iter begin, Sent end, size_t size)
{
    return std::ranges::subrange(begin, end, size);
}
template<std::ranges::range RangeLike>
auto& make_range(RangeLike& range)
{
    return range;
}
template<std::ranges::range RangeLike>
auto make_range(RangeLike& range, std::ranges::range_size_t<RangeLike> size)
{
    return std::ranges::subrange(range, size);
}

/// const_cast for avoiding code duplication
/// only use this if you are sure that the constness is actually a lie!
template<typename T>
constexpr T& as_mutable(T const& value) noexcept
{
    return const_cast<T&>(value);
}
template<typename T>
constexpr T* as_mutable(T const* value) noexcept
{
    return const_cast<T*>(value);
}
template<typename T>
constexpr T* as_mutable(T* value) noexcept
{
    return value;
}
template<typename T>
void as_mutable(T const&&) = delete;

void for_each_tag(auto&& f)
{
    f(mimir::Static {});
    f(mimir::Fluent {});
    f(mimir::Derived {});
}

template<typename IndexType, IndexType... Is>
void for_each_index(auto&& f)
{
    (f(std::integral_constant<IndexType, Is> {}), ...);
}

}
