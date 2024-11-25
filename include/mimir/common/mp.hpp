#include "macros.hpp"
#include "type_traits.hpp"

#include <algorithm>
#include <cstddef>
#include <tuple>

namespace mimir::mp
{


template<std::size_t N, typename Seq>
struct offset_integer_sequence;

template<std::size_t N, std::size_t... Ints>
struct offset_integer_sequence<N, std::index_sequence<Ints...>>
{
    using type = std::index_sequence<Ints + N...>;
    static constexpr size_t size() noexcept { return sizeof...(Ints); }
};
template<std::size_t N, typename Seq>
using offset_index_sequence = typename offset_integer_sequence<N, Seq>::type;

template<std::size_t N, size_t I, size_t... Is>
using make_offset_index_sequence =
    typename offset_integer_sequence<N, std::conditional_t<sizeof...(Is) == 0, std::make_index_sequence<I>, std::index_sequence<I, Is...>>>::type;

// To create index_sequence<3, 4, 5, 6>:
//      offset_index_sequence<3, std::make_index_sequence<4>>;

// Function to cap an index sequence at a given max index and remove duplicates
template <std::size_t UpperBound, std::size_t... Is>
constexpr auto upper_bounded_index_sequence(std::index_sequence<Is...>) {
    // Filter and cap indices, ensuring uniqueness
    constexpr std::array<std::size_t, sizeof...(Is)> capped_indices = {
        (Is < UpperBound ? Is : UpperBound - 1)...
    };

    // Deduplicate
    constexpr std::array<std::size_t, sizeof...(Is)> unique_indices = [] {
        std::array<std::size_t, sizeof...(Is)> temp = {};
        std::size_t count = 0;
        for (std::size_t i = 0; i < capped_indices.size(); ++i) {
            if (i == 0 || capped_indices[i] != capped_indices[i - 1]) {
                temp[count++] = capped_indices[i];
            }
        }
        return temp;
    }();

    // Determine the size of the unique array
    constexpr std::size_t unique_size = [] {
        std::size_t count = 0;
        for (std::size_t i = 0; i < unique_indices.size(); ++i) {
            if (i == 0 || unique_indices[i] != unique_indices[i - 1]) {
                count++;
            }
        }
        return count;
    }();

    // Generate the resulting unique index sequence
    return []<std::size_t... Us>(std::index_sequence<Us...>) {
        return std::index_sequence<unique_indices[Us]...>{};
    }(std::make_index_sequence<unique_size>{});
}

template<typename T>
    requires is_specialization_v<raw_t<T>, std::tuple>
constexpr size_t size()
{
    return std::tuple_size_v<raw_t<T>>;
};
template<typename T>
    requires is_specialization_v<raw_t<T>, cista::tuple>
constexpr size_t size()
{
    return cista::tuple_size_v<raw_t<T>>;
};

template<typename... Ts>
constexpr auto sizes(Ts&&... tuples)
{
    return std::tuple(size(tuples)...);
}

template<typename T1, typename T2, typename... Ts>
constexpr auto min(T1&& v1, T2&& v2, Ts&&... values)
{
    if constexpr (sizeof...(values) == 0)
    {
        return std::min(v1, v2);
    }
    else
    {
        return std::min(v1, min(v2, values...));
    }
}

template<typename T1, typename T2, typename... Ts>
constexpr auto max(T1&& v1, T2&& v2, Ts&&... values)
{
    if constexpr (sizeof...(values) == 0)
    {
        return std::max(v1, v2);
    }
    else
    {
        return std::max(v1, max(v2, values...));
    }
}
namespace detail
{
using std::get;

template<size_t I, typename Tuple>
    requires is_specialization_v<raw_t<Tuple>, cista::tuple>
decltype(auto) get(Tuple&& t)
{
    return cista::get<I>(FWD(t));
}

// helper to extract an element from each tuple at a given index
template<std::size_t I, typename... Tuples>
auto get_each(Tuples&&... tuples)
{
    return std::forward_as_tuple(get<I>(FWD(tuples))...);
}

template<typename... Tuples, std::size_t... Is>
auto zip_at_impl(std::index_sequence<Is...>, Tuples&&... tuples)
{
    return std::forward_as_tuple(detail::get_each<Is>(FWD(tuples)...)...);
}

}

template<typename... Tuples, std::size_t... Is>
auto zip_at(std::index_sequence<Is...>, Tuples&&... tuples)
{
    constexpr size_t max_index = min(max(Is...), sizeof...(Tuples));
    return detail::zip_at_impl(std::make_index_sequence<max_index> {}, FWD(tuples)...);
}

template<typename... Tuples>
auto zip(Tuples&&... tuples)
{
    return zip_at(std::make_index_sequence<min(size<Tuples>()...)> {}, FWD(tuples)...);
}


template<typename... Tuples>
auto zip_even(Tuples&&... tuples)
{
    return zip_at(std::index_sequence_for<size<Tuples>()> {}, FWD(tuples)...);
}

}