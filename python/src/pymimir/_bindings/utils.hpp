
#pragma once

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace pymimir
{
template<typename T>
requires(not std::derived_from<T, py::handle>)
inline py::object cast_safe(T&& obj)
{
    auto pyobj = py::cast(FWD(obj));
    if (!pyobj)
    {
        throw py::error_already_set();
    }
    return pyobj;
}
inline py::handle cast_safe(py::handle obj)
{
    return obj;
}

py::list init_list(auto&& rng)
{
    if constexpr (not std::derived_from<raw_t<decltype(rng)>, py::handle>)
    {
        if constexpr (std::ranges::sized_range<raw_t<decltype(rng)>>)
        {
            return py::list(std::ranges::size(rng));
        }
        else
        {
            return py::list(0);
        }
    }
    else
    {
        if (py::isinstance<py::sequence>(rng))
        {
            return py::list(py::cast<py::sequence>(rng).size());
        }
        else
        {
            return py::list(0);
        }
    }
}

template<typename ListT>
    requires std::same_as<raw_t<ListT>, py::list>
decltype(auto) insert_into_list(const py::object& self, ListT&& lst, std::ranges::range auto&& rng, std::integral auto& counter)
{
    for (auto&& elem : FWD(rng))
    {
        auto pyelem = cast_safe(FWD(elem));
        py::detail::keep_alive_impl(pyelem, self);
        lst[counter] = std::move(pyelem);
        ++counter;
    }
    return FWD(lst);
}
template<typename ListT>
    requires std::same_as<raw_t<ListT>, py::list>
decltype(auto) insert_into_list(ListT&& lst, std::ranges::range auto&& rng, std::integral auto& counter)
{
    for (auto&& elem : FWD(rng))
    {
        auto pyelem = cast_safe(FWD(elem));
        lst[counter] = std::move(pyelem);
        ++counter;
    }
    return FWD(lst);
}
template<typename ListT>
    requires std::same_as<raw_t<ListT>, py::list>
decltype(auto) insert_into_list(const py::object& self, ListT&& lst, std::ranges::range auto&& rng)
{
    for (auto&& elem : FWD(rng))
    {
        auto pyelem = cast_safe(FWD(elem));
        py::detail::keep_alive_impl(pyelem, self);
        lst.append(std::move(pyelem));
    }
    return FWD(lst);
}

template<typename ListT>
    requires std::same_as<raw_t<ListT>, py::list>
decltype(auto) insert_into_list(ListT&& lst, std::ranges::range auto&& rng)
{
    for (auto&& elem : FWD(rng))
    {
        lst.append(cast_safe(FWD(elem)));
    }
    return FWD(lst);
}

inline py::list insert_into_list(const py::object& self, std::ranges::range auto&& rng)
{
    py::list lst = init_list(rng);
    if (lst.size() > 0)
    {
        size_t counter = 0;
        insert_into_list(self, lst, FWD(rng), counter);
        return lst;
    }
    else
    {
        return insert_into_list(self, lst, FWD(rng));
    }
}

inline py::list insert_into_list(std::ranges::range auto&& rng)
{
    py::list lst = init_list(rng);
    if (lst.size() > 0)
    {
        size_t counter = 0;
        insert_into_list(lst, FWD(rng), counter);
        return lst;
    }
    else
    {
        return insert_into_list(lst, FWD(rng));
    }
}

template<bool positive, PredicateTag P, typename Self>
GroundAtomList<P> atoms_from_conditions(const Self& self, const PDDLRepositories& factories)
{
    if constexpr (positive)
        return factories.get_ground_atoms_from_indices<P>(self.template get_positive_precondition<P>());
    else
        return factories.get_ground_atoms_from_indices<P>(self.template get_negative_precondition<P>());
};

template<bool positive, typename Self>
py::list all_atoms_from_conditions(const Self& self, const py::object& py_factories)
{
    const auto& factories = py::cast<const PDDLRepositories&>(py_factories);

    auto fluent_atoms = atoms_from_conditions<positive, Fluent>(self, factories);
    auto static_atoms = atoms_from_conditions<positive, Static>(self, factories);
    auto derived_atoms = atoms_from_conditions<positive, Derived>(self, factories);

    py::list lst(fluent_atoms.size() + static_atoms.size() + derived_atoms.size());
    size_t counter = 0;
    insert_into_list(py_factories, lst, fluent_atoms, counter);
    insert_into_list(py_factories, lst, static_atoms, counter);
    insert_into_list(py_factories, lst, derived_atoms, counter);
    return lst;
};

constexpr auto cast_visitor = AS_LAMBDA(py::cast);
constexpr auto repr_visitor = AS_LAMBDA([](const auto& arg) { return arg.str(); });

void for_each_tag(auto&& f)
{
    f(Static {});
    f(Fluent {});
    f(Derived {});
}

template<typename IndexType, IndexType... Is>
void for_each_index(auto&& f)
{
    (f(std::integral_constant<IndexType, Is> {}), ...);
}

template<PredicateTag P>
constexpr std::string tag_name()
{
    if constexpr (std::is_same_v<P, Static>)
    {
        return "Static";
    }
    else if constexpr (std::is_same_v<P, Fluent>)
    {
        return "Fluent";
    }
    else if constexpr (std::is_same_v<P, Derived>)
    {
        return "Derived";
    }
    else
    {
        static_assert(dependent_false<P>::value, "non-exhaustive visitor!");
    }
}

/// @brief Binds a std::span<T> as an unmodifiable python object.
/// Modifiable std::span are more complicated.
/// Hence, we use std::span exclusively for unmodifiable data,
/// and std::vector for modifiable data.
template<typename Span, typename holder_type = std::unique_ptr<Span>>
py::class_<Span, holder_type> bind_const_span(py::handle m, const std::string& name)
{
    py::class_<Span, holder_type> cl(m, name.c_str());

    using T = typename Span::value_type;
    using SizeType = typename Span::size_type;
    using DiffType = typename Span::difference_type;
    using ItType = typename Span::iterator;

    auto wrap_i = [](DiffType i, SizeType n)
    {
        if (i < 0)
        {
            i += n;
        }
        if (i < 0 || (SizeType) i >= n)
        {
            throw py::index_error();
        }
        return i;
    };

    cl.def(
        "__getitem__",
        [wrap_i](const Span& v, DiffType i) -> const T&
        {
            i = wrap_i(i, v.size());
            return v[(SizeType) i];
        },
        py::return_value_policy::reference_internal  // ref + keepalive
    );

    cl.def(
        "__iter__",
        [](const Span& v) { return py::make_iterator<py::return_value_policy::reference_internal, ItType, ItType, const T&>(v.begin(), v.end()); },
        py::keep_alive<0, 1>() /* Essential: keep span alive while iterator exists */
    );

    cl.def("__len__", &Span::size);

    return cl;
}

/// @brief Binds a IndexGroupedVector as an unmodifiable python object.
/// Modifiable IndexGroupedVector are more complicated because they use std::span.
/// See section regarding bind_const_span.
template<typename IndexGroupedVector, typename holder_type = std::unique_ptr<IndexGroupedVector>>
py::class_<IndexGroupedVector, holder_type> bind_const_index_grouped_vector(py::handle m, const std::string& name)
{
    py::class_<IndexGroupedVector, holder_type> cl(m, name.c_str());

    using T = typename IndexGroupedVector::value_type;
    using SizeType = typename IndexGroupedVector::size_type;
    using DiffType = typename IndexGroupedVector::difference_type;
    using ItType = typename IndexGroupedVector::const_iterator;

    auto wrap_i = [](DiffType i, SizeType n)
    {
        if (i < 0)
        {
            i += n;
        }
        if (i < 0 || (SizeType) i >= n)
        {
            throw py::index_error();
        }
        return i;
    };

    /**
     * Accessors
     */

    /* This requires std::vector<T> to be an opague type
       because weak references to python lists are not allowed.
    */
    cl.def(
        "__getitem__",
        [wrap_i](const IndexGroupedVector& v, DiffType i) -> T
        {
            i = wrap_i(i, v.size());
            return v[(SizeType) i];
        },
        py::return_value_policy::copy,
        py::keep_alive<0, 1>()); /* Essential keep span alive while copy of element exists */

    cl.def(
        "__iter__",
        [](const IndexGroupedVector& v)
        {
            return py::make_iterator<py::return_value_policy::copy, ItType, ItType, T>(
                v.begin(),
                v.end(),
                py::return_value_policy::copy,
                py::keep_alive<0, 1>()); /* Essential: keep iterator alive while copy of element exists. */
        },
        py::keep_alive<0, 1>() /* Essential: keep index grouped vector alive while iterator exists */
    );

    cl.def("__len__", &IndexGroupedVector::size);

    return cl;
}

}  // namespace pymimir
