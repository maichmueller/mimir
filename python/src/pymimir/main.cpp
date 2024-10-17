#include "_bindings/init_declarations.hpp"
#include "_bindings/opaque_types.hpp"
#include "_bindings/utils.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>  // Necessary for automatic conversion of e.g. std::vectors
#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

using namespace mimir;
using namespace pymimir;

namespace py = pybind11;



PYBIND11_MODULE(_pymimir, m)
{
    m.doc() = "Python bindings for the Mimir planning library.";

    init_pymimir(m);

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
