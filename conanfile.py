import os
from pathlib import Path

from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain
from conan.tools.files import load


# Boost components from its conanfile recipe
# https://github.com/conan-io/conan-center-index/blob/master/recipes/boost/all/conanfile.py
BOOST_COMPS = (
    "atomic",
    "charconv",
    "chrono",
    "cobalt",
    "container",
    "context",
    "contract",
    "coroutine",
    "date_time",
    "exception",
    "fiber",
    "filesystem",
    "graph",
    "graph_parallel",
    "iostreams",
    "json",
    "locale",
    "log",
    "math",
    "mpi",
    "nowide",
    "process",
    "program_options",
    "python",
    "random",
    "regex",
    "serialization",
    "stacktrace",
    "system",
    "test",
    "thread",
    "timer",
    "type_erasure",
    "url",
    "wave",
)


class MimirRecipe(ConanFile):
    name = "mimir"
    package_type = "library"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"

    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_benchmark": [True, False],
    }

    default_options = {
        "shared": False,
        "fPIC": True,
        "with_benchmark": False,
        "cista/*:with_fmt": True,
    }
    default_options.update({f"boost/*:without_{comp}": True for comp in BOOST_COMPS})
    for comp in ("iostreams", "random", "regex", "system"):
        default_options.update({f"boost/*:without_{comp}": False})

    def set_version(self):
        self.version = load(
            self, os.path.join(Path(__file__).resolve().parent, "__version__")
        )

    def requirements(self):
        requirements = self.conan_data.get("requirements", [])
        for requirement in requirements:
            self.requires(requirement)
        if self.options.with_benchmark:
            self.requires("benchmark/1.9.0")

    def validate(self):
        # mimir requires C++20
        check_min_cppstd(self, "20")

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.user_presets_path = False
        tc.generate()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["mimir"]
