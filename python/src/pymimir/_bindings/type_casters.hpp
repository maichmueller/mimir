#pragma once

#include "mimir/mimir.hpp"
#include "pybind11/pybind11.h"
#include "utils.hpp"

namespace py = pybind11;
using namespace mimir;

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

// Custom type caster for FlatIndexList
namespace pybind11::detail
{
template<>
struct type_caster<FlatIndexList>
{
public:
    // pybind11 requires these member functions
    PYBIND11_TYPE_CASTER(FlatIndexList, _("FlatIndexList"));

    // Conversion: Python -> C++
    bool load(handle src, bool)
    {
        // Try to extract a Python object as a list
        if (!py::isinstance<py::iterable>(src))
            return false;

        value.clear();
        py::iterable iterable = std::invoke(
            [&]() -> py::iterable
            {
                try
                {
                    auto py_list = src.cast<py::list>();
                    value.reserve(py_list.size());
                    return py_list;
                }
                catch (const py::cast_error& e)
                {
                    try
                    {
                        return py::cast<py::iterable>(src);
                    }
                    catch (const py::cast_error& e)
                    {
                        return {};
                    }
                }
            });

        // Convert each element from Python to C++
        for (const auto& item : iterable)
        {
            value.push_back(item.cast<Index>());
        }
        return true;
    }

    // Conversion: C++ -> Python
    static handle cast(const FlatIndexList& src, return_value_policy, handle)
    {
        return mimir::pymimir::insert_into_list(src);
    }
};
}
