
#include "init_declarations.hpp"
#include "mimir/cista/containers/dynamic_bitset.h"
#include "opaque_types.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace pymimir;

void init_cista_types(py::module& m)
{
    class_<FlatBitset>(m, "FlatBitset")  //
        .def(py::init<>())               // expose default constructor
        .def(py::init<size_t>())         // expose constructor that accepts number of bits
        .def(py::init<size_t, bool>())   // expose constructor that accepts num_bits and default_bit_value
        .def("get", &FlatBitset::get, py::arg("position"), "Get the value of the bit at a specific position")
        .def("set", &FlatBitset::set, py::arg("position"), "Set the bit at the specified position")
        .def("unset", &FlatBitset::unset, py::arg("position"), "Unset the bit at the specified position")
        .def("unset_all", &FlatBitset::unset_all, "Unset all bits")
        .def("count", &FlatBitset::count, "Count the number of set bits")
        .def("next_set_bit", &FlatBitset::next_set_bit, py::arg("position"), "Find the next set bit from a position")
        .def("shrink_to_fit", &FlatBitset::shrink_to_fit, "Shrink the bitset to fit its contents")
        .def("resize_to_fit", &FlatBitset::resize_to_fit, py::arg("other"), "Resize to fit another bitset")
        .def("__or__", &FlatBitset::operator|, py::arg("other"), "Bitwise OR with another bitset")
        .def("__ior__", &FlatBitset::operator|=, py::arg("other"), "Bitwise OR assignment with another bitset")
        .def("__and__", &FlatBitset::operator&, py::arg("other"), "Bitwise AND with another bitset")
        .def("__iand__", &FlatBitset::operator&=, py::arg("other"), "Bitwise AND assignment with another bitset")
        .def("__sub__", &FlatBitset::operator-, py::arg("other"), "Bitwise subtraction with another bitset")
        .def("__isub__", &FlatBitset::operator-=, py::arg("other"), "Bitwise subtraction assignment with another bitset")
        .def("is_superseteq", &FlatBitset::is_superseteq, py::arg("other"), "Check if the bitset is a superset or equal to another")
        .def("are_disjoint", &FlatBitset::are_disjoint, py::arg("other"), "Check if two bitsets are disjoint")
        .def("__invert__", &FlatBitset::operator~, "Invert all bits in the bitset")
        .def(
            "__iter__",
            [](const FlatBitset& bs) { return py::make_iterator(bs.begin(), bs.end()); },
            py::keep_alive<0, 1>(),
            "Return an iterator over the set bits")
        .def(auto_pickler<FlatBitset>());
}
