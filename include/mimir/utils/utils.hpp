
#pragma once

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

template<typename Iter, typename Sent>
class RangeWrapper
{
public:
    RangeWrapper(Iter begin, Sent end) : m_begin(begin), m_end(end) {}

    auto begin() const { return m_begin; }
    auto end() const { return m_end; }

    size_t size() const
    {
        return std::ranges::distance(m_begin, m_end);
    }

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

/// const_cast for avoiding code duplication
/// only use this if you are sure that the constness is actually a lie!
template<typename T>
constexpr T & as_mutable(T const & value) noexcept {
    return const_cast<T &>(value);
}
template<typename T>
constexpr T * as_mutable(T const * value) noexcept {
    return const_cast<T *>(value);
}
template<typename T>
constexpr T * as_mutable(T * value) noexcept {
    return value;
}
template<typename T>
void as_mutable(T const &&) = delete;


}
