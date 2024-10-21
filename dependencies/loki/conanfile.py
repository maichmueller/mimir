from conan import ConanFile
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain
from conan.tools.scm import Git


class LokiRecipe(ConanFile):
    name = "loki"
    version = "0.0.6"
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
    }

    def requirements(self):
        self.requires("boost/1.86.0")

    def source(self):
        git = Git(self)
        git.clone("https://github.com/drexlerd/Loki.git", target=".")
        git.checkout(f"v{self.version}")

    def configure(self):
        # if windows remove the fPIC option, it has no effect
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def validate(self):
        # loki requires C++20
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
        self.cpp_info.libs = ["loki"]
