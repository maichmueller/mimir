from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, cmake_layout, CMakeToolchain
from conan.tools.files import (
    copy,
    download,
    apply_conandata_patches,
    export_conandata_patches,
    get,
)
from conan.tools.layout import basic_layout
from conan.tools.scm import Version, Git
import os

required_conan_version = ">=1.52.0"

# taken form conan-center-index recipe, until latest bug fix release is available, then delete this and move to main package


class UnorderedDenseCistaConan(ConanFile):
    name = "unordered_dense_cista"
    description = "A fast & densely stored hashmap and hashset based on robin-hood backward shift deletion"
    license = "MIT"
    url = "https://github.com/conan-io/conan-center-index"
    homepage = "https://github.com/martinus/unordered_dense"
    topics = ("unordered_map", "unordered_set", "hashmap", "hashset", "header-only")
    package_type = "header-library"
    settings = "os", "arch", "compiler", "build_type"
    no_copy_source = True

    @property
    def _minimum_cpp_standard(self):
        return 17

    @property
    def _compilers_minimum_version(self):
        return {
            "Visual Studio": "15.7",
            "msvc": "191",
            "gcc": "7",
            "clang": "7",
            "apple-clang": "11",
        }

    def layout(self):
        basic_layout(self, src_folder="src")

    def package_id(self):
        self.info.clear()

    def validate(self):
        if self.settings.get_safe("compiler.cppstd"):
            check_min_cppstd(self, self._minimum_cpp_standard)
        minimum_version = self._compilers_minimum_version.get(
            str(self.settings.compiler), False
        )
        if (
            minimum_version
            and Version(self.settings.get_safe("compiler.version")) < minimum_version
        ):
            raise ConanInvalidConfiguration(
                f"{self.ref} requires C++{self._minimum_cpp_standard}, which your compiler does not support."
            )

    def source(self):
        for file in self.conan_data["sources"][self.version]:
            if "type" in file and file["type"] == "git":
                git = Git(self)
                git.clone(file["url"], target=".")
                git.checkout(file["commit"])
            else:
                if file["url"].endswith(".tar.gz"):
                    get(
                        self,
                        strip_root=True,
                        **file,
                    )
                else:
                    filename = os.path.basename(file["url"])
                    download(self, filename=filename, **file)
        apply_conandata_patches(self)

    def build(self):
        pass

    def package(self):
        copy(
            self,
            pattern="LICENSE",
            dst=os.path.join(self.package_folder, "licenses"),
            src=self.source_folder,
        )
        copy(
            self,
            pattern="*.h",
            dst=os.path.join(self.package_folder, "include", "ankerl"),
            src=os.path.join(self.source_folder, "include", "ankerl"),
        )

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []

        self.cpp_info.set_property("cmake_file_name", "unordered_dense")
        self.cpp_info.set_property("cmake_target_name", "cista::unordered_dense")
