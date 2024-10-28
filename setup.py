import glob
import json
import os
import sys
import subprocess
import multiprocessing
import shutil

from pathlib import Path

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext

with open("__version__", "r") as f:
    version = f.read().strip()
    print(version)


HERE = Path(__file__).resolve().parent


def get_conan_path():
    try:
        # Use 'which' to find the conan binary in a POSIX system
        if sys.platform.startswith("win"):
            result = subprocess.run(
                ["where", "conan"], stdout=subprocess.PIPE, check=True
            )
        else:
            result = subprocess.run(
                ["which", "conan"], stdout=subprocess.PIPE, check=True
            )

        conan_path = result.stdout.decode().strip()
        if conan_path:
            return conan_path
        else:
            raise FileNotFoundError("Conan executable not found.")
    except subprocess.CalledProcessError as e:
        print("Subprocess error.")
        raise e


conan_cmd = get_conan_path()
conan_extra_args = "--build=missing"


# A CMakeExtension needs a sourcedir instead of a file list.
# The name must be the _single_ output extension from the CMake build.
# If you need multiple extensions, see scikit-build.
class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def build_extension(self, ext):
        global version
        # Must be in this form due to bug in .resolve() only fixed in Python 3.10+
        ext_fullpath = Path.cwd() / self.get_ext_fullpath(ext.name)
        output_directory = ext_fullpath.parent.resolve()
        temp_directory = Path.cwd() / self.build_temp

        print("ext_fullpath", ext_fullpath)
        print("output_directory", output_directory)
        print("temp_directory", temp_directory)

        config = "Debug" if os.environ.get("PYMIMIR_DEBUG_BUILD") else "Release"
        print("Pymimir build type:", config)

        # Create the temporary build directory, if it does not already exist
        os.makedirs(temp_directory, exist_ok=True)

        if os.getenv("CIBUILDWHEEL") == "1":
            sys.path.append(str(HERE))  # needed to find conan_ci.py
            import conan_ci

            conan_ci.main(conan_cmd, conan_extra_args=conan_extra_args)

        print("CONAN_HOME:", os.getenv("CONAN_HOME"))

        # Build Pymimir

        # 1. delete build directory cache
        for file in glob.glob(f"{temp_directory}/**/CMakeCache.txt", recursive=True):
            print(f"Removing CMakeCache.txt: ", file)
            os.remove(file)

        build_folder = f"{temp_directory / 'build'}"

        configure_cmd = [
            "./configure.sh",
            "--source",
            ext.sourcedir,
            "--build",
            build_folder,
            "--config",
            config,
            "--conan_cmd",
            conan_cmd,
            "--conan_extra_args",
            conan_extra_args,
            f"-DBUILD_PYMIMIR=ON",
            f"-DBUILD_TESTS=OFF",
            f"-DMIMIR_VERSION_INFO={version}",
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={output_directory}",
            f"-DPYTHON_EXECUTABLE={sys.executable}",
        ]
        subprocess.check_call(
            configure_cmd,
            cwd=HERE,
        )
        build_args = ["--target", ext.name]
        subprocess.run(
            [
                "cmake",
                "--build",
                build_folder,
                "--parallel",
            ]
            + build_args,
            cwd=str(temp_directory),
            check=True,
        )

        # Generate pyi stub files

        # This is safer than adding a custom command in cmake because it will not silently fail.
        subprocess.run(
            [
                sys.executable,
                "-m",
                "pybind11_stubgen",
                "--output-dir",
                temp_directory,
                "_pymimir",
            ],
            cwd=output_directory,
            check=True,
        )

        # Create package output directory
        os.makedirs(output_directory / "pymimir", exist_ok=True)

        # Copy the stubs from temp to suitable output directory
        # The name has to match the package initialization pymimir/__init__.py,
        # i.e., pymimir/__init__.pyi to ensure full IDE support.
        shutil.copy(
            temp_directory / "_pymimir.pyi",
            output_directory / "pymimir" / "__init__.pyi",
        )


setup(
    ext_modules=[CMakeExtension("_pymimir")],
    cmdclass={"build_ext": CMakeBuild},
)
