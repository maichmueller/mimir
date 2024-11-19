
#include "init_declarations.hpp"
#include "opaque_types.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>

namespace py = pybind11;

using namespace mimir;

void init_cista_types(py::module& m)
{
    class_<FlatBitset>(m, "FlatBitset")  //
        .def(py::init<>())                   // expose default constructor
        .def(py::init<size_t>())             // expose constructor that accepts number of bits
        .def(py::init<size_t, bool>())       // expose constructor that accepts num_bits and default_bit_value
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
            "Return an iterator over the set bits");
    
    // Custom bindings for FlatIndexList
    py::class_<FlatIndexList>(m, "FlatIndexList")
        .def(py::init<>())  // Default constructor
        .def(py::init<FlatIndexList::size_type, Index>(), py::arg("size"), py::arg("init") = 0)  // Size constructor with default value
        .def(py::init<std::initializer_list<Index>>(), py::arg("init"))  // Initializer list constructor
        .def("size", &FlatIndexList::size)  // size() method
        .def("empty", &FlatIndexList::empty)  // empty() method
        .def("push_back", &FlatIndexList::push_back, py::arg("value"))  // push_back method
        .def("pop_back", &FlatIndexList::pop_back)  // pop_back method
        .def("resize", &FlatIndexList::resize, py::arg("size"), py::arg("init") = 0)  // resize method with default value
        .def("clear", &FlatIndexList::clear)  // clear() method
        .def("at", py::overload_cast<FlatIndexList::access_type>(&FlatIndexList::at), py::arg("index"))  // at() method for non-const
        .def("at", py::overload_cast<FlatIndexList::access_type const>(&FlatIndexList::at, py::const_), py::arg("index"))  // at() method for const
        .def("__getitem__", [](const FlatIndexList &v, size_t i) {
                 if (i >= v.size()) throw std::out_of_range("Index out of range");
                 return v[i];
             })  // __getitem__ for indexing
        .def("__setitem__", [](FlatIndexList &v, size_t i, Index value) {
                 if (i >= v.size()) throw std::out_of_range("Index out of range");
                 v[i] = value;
             })  // __setitem__ for indexing
        .def("__len__", &FlatIndexList::size)  // __len__ for Python len() support
        .def("__iter__", [](FlatIndexList &v) { return py::make_iterator(v.begin(), v.end()); },
             py::keep_alive<0, 1>())  // Iterator support for Python
        .def("data", py::overload_cast<>(&FlatIndexList::data, py::const_))  // data() method
        .def("front", py::overload_cast<>(&FlatIndexList::front), py::return_value_policy::reference)  // front() method
        .def("back", py::overload_cast<>(&FlatIndexList::back), py::return_value_policy::reference);  // back() method
}
