import os
from pathlib import Path

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain


project_folder = Path(__file__).resolve().parent.parent.parent


def get_version():
    with open(os.path.join(project_folder, "__version__"), "r") as f:
        return f.read().strip()


class MimirIntergrationTestRecipe(ConanFile):
    name = "mimir-integration-test"
    version = get_version()
    package_type = "static-library"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"

    def requirements(self):
        self.requires(self.tested_reference_str)

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

    def test(self):
        if can_run(self):
            cmd = os.path.join(self.cpp.build.bindir, "integration_standalone")
            self.run(cmd, env="conanrun")
