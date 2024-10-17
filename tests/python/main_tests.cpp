#include "_bindings/init_declarations.hpp"

#include <gtest/gtest.h>
#include <pybind11/pybind11.h>
// necessary to replicate the module here to use it in-place (verify against python/src/mimir/main.cpp)
PYBIND11_MODULE(_pymimir, m) { init_pymimir(m); }

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}