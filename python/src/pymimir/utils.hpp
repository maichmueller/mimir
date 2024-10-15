
#ifndef PYMIMIR_UTILS_H
#define PYMIMIR_UTILS_H

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace pymimir
{
inline py::object cast_safe(const auto& obj)
{
    auto pyobj = py::cast(obj);
    if (!pyobj)
    {
        throw py::error_already_set();
    }
    return pyobj;
}

inline void insert_into_list(const py::object& self, py::list& lst, const std::ranges::range auto& rng, std::integral auto& counter)
{
    for (const auto& elem : rng)
    {
        auto pyelem = cast_safe(elem);
        py::detail::keep_alive_impl(pyelem, self);
        lst[counter] = std::move(pyelem);
        ++counter;
    }
}

inline void insert_into_list(py::list& lst, const std::ranges::range auto& rng, std::integral auto& counter)
{
    for (const auto& elem : rng)
    {
        auto pyelem = cast_safe(elem);
        lst[counter] = std::move(pyelem);
        ++counter;
    }
}

inline void insert_into_list(const py::object& self, py::list& lst, const std::ranges::range auto& rng)
{
    for (const auto& elem : rng)
    {
        auto pyelem = cast_safe(elem);
        py::detail::keep_alive_impl(pyelem, self);
        lst.append(std::move(pyelem));
    }
}

inline void insert_into_list(py::list& lst, const std::ranges::range auto& rng)
{
    for (const auto& elem : rng)
    {
        lst.append(cast_safe(elem));
    }
}

inline py::list insert_into_list(const py::object& self, const std::ranges::range auto& rng)
{
    if constexpr (std::ranges::sized_range<decltype(rng)>)
    {
        py::list lst(rng.size());
        size_t counter = 0;
        insert_into_list(self, lst, rng, counter);
        return lst;
    }
    else
    {
        py::list lst(0);
        size_t counter = 0;
        insert_into_list(self, lst, rng, counter);
        return lst;
    }
}

inline py::list insert_into_list(const std::ranges::range auto& rng)
{
    if constexpr (std::ranges::sized_range<decltype(rng)>)
    {
        py::list lst(rng.size());
        size_t counter = 0;
        insert_into_list(lst, rng, counter);
        return lst;
    }
    else
    {
        py::list lst(0);
        size_t counter = 0;
        insert_into_list(lst, rng, counter);
        return lst;
    }
}
}  // namespace pymimir

#endif  // PYMIMIR_UTILS_H
