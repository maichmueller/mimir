from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain


class Loki(ConanFile):
    name = "loki"
    version = "0.0.6"
    package_type = "static-library"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"


    def requirements(self):
        self.requires("boost/1.86.0")

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
