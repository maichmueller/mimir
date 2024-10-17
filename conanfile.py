from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain


class Mimir(ConanFile):
    name = "mimir"
    version = "0.0.1.dev"
    package_type = "static-library"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"


    def requirements(self):
        requirements = self.conan_data.get("requirements", [])
        for requirement in requirements:
            self.requires(requirement)

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
