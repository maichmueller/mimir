from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain


def get_version():
    with open("__version__", "r") as f:
        return f.read().strip()


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
    version = get_version()
    package_type = "static-library"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"

    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }

    default_options = {
        "shared": False,
        "fPIC": True,
        "cista/*:with_fmt": True,
    }
    default_options.update({f"boost/*:without_{comp}": True for comp in BOOST_COMPS})
    for comp in ("iostreams", "random", "regex", "system"):
        default_options.update({f"boost/*:without_{comp}": False})

    def requirements(self):
        requirements = self.conan_data.get("requirements", [])
        for requirement in requirements:
            self.requires(requirement)

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
