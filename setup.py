import os
import sys
import subprocess
import multiprocessing
import shutil
import sysconfig

from pathlib import Path

from setuptools import setup, find_packages, Extension
from setuptools.command.build_ext import build_ext


# Single source of truth for the pymimir wheel version and for MIMIR_VERSION_INFO.
# Bumping rules live in docs/VERSIONING.md -- in short, a new binding-visible feature bumps
# the MINOR component, so downstream can gate on `pymimir>=X.Y` instead of probing with
# `hasattr`. 0.14.2: packaging/CI overhaul (Python >= 3.12 incl. free-threaded wheels).
# 0.14.3: source builds honour the pinned nanobind instead of reusing a stale prefix.
# 0.15.0: nanobind 3 (internals generation 22). MINOR rather than PATCH despite
#   being a build change -- see docs/VERSIONING.md: every extension sharing
#   NB_DOMAIN=pymimir_abi_domain must be rebuilt against the same generation, and
#   a downstream needs to express that as a resolvable floor rather than discover
#   it as a TypeError at the first cross-module cast.
# 0.15.1: native all-private landmark novelty grouping for disjunctive LIW.
__version__ = "0.15.1"
HERE = Path(__file__).resolve().parent


# A CMakeExtension needs a sourcedir instead of a file list.
# The name must be the _single_ output extension from the CMake build.
# If you need multiple extensions, see scikit-build.
class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = Path(os.path.abspath(sourcedir))


class CMakeBuild(build_ext):
    def build_extension(self, ext):
        # Must be in this form due to bug in .resolve() only fixed in Python 3.10+
        ext_fullpath = Path.cwd() / self.get_ext_fullpath(ext.name)
        output_directory = ext_fullpath.parent.resolve()
        temp_directory = Path.cwd() / self.build_temp

        print("ext_fullpath", ext_fullpath)
        print("output_directory", output_directory)
        print("temp_directory", temp_directory)

        build_type = "Debug" if os.environ.get('PYMIMIR_DEBUG_BUILD') else "Release"
        print("Pymimir build type:", build_type)

        # Create the temporary build directory, if it does not already exist
        os.makedirs(temp_directory, exist_ok=True)

        # CI can point PYMIMIR_DEPENDENCY_PREFIX at a cached dependency install.
        # The effective prefix is scoped by the platform/libc tag so caches for
        # different wheel variants never collide within one shared directory.
        dependency_prefix = os.environ.get("PYMIMIR_DEPENDENCY_PREFIX")
        if dependency_prefix:
            dependency_prefix = Path(dependency_prefix)
            if not dependency_prefix.is_absolute():
                dependency_prefix = (Path.cwd() / dependency_prefix).resolve()
            dependency_scope = os.environ.get("AUDITWHEEL_PLAT")
            if not dependency_scope:
                dependency_scope = sysconfig.get_platform()
            dependency_prefix = dependency_prefix / dependency_scope
        else:
            dependency_prefix = temp_directory / "dependencies" / "installs"

        cmake_args = [
            f"-DCMAKE_BUILD_TYPE={build_type}",
            f"-DCMAKE_INSTALL_PREFIX={str(dependency_prefix)}",
            f"-DCMAKE_PREFIX_PATH={str(dependency_prefix)}",
            f"-DPython_EXECUTABLE={sys.executable}"
        ]

        subprocess.run(
            ["cmake", "-S", f"{str(ext.sourcedir / 'dependencies')}", "-B", f"{str(temp_directory / 'dependencies' / 'build')}"] + cmake_args, cwd=str(temp_directory), check=True
        )

        subprocess.run(
            ["cmake", "--build", f"{str(temp_directory / 'dependencies' / 'build')}", f"-j{multiprocessing.cpu_count()}"]
        )

        subprocess.run(
            ["cmake", "--install", f"{str(temp_directory / 'dependencies' / 'build')}"]
        )

        shutil.rmtree(f"{str(temp_directory / 'dependencies' / 'build')}")

        #######################################################################
        # Build mimir
        #######################################################################

        cmake_args = [
            "-DBUILD_PYMIMIR=ON",
            "-DMIMIR_BUILD_SHARED_CORE=ON",
            "-DCMAKE_INSTALL_LIBDIR=lib",
            "-DCMAKE_INSTALL_BINDIR=bin",
            "-DCMAKE_INSTALL_INCLUDEDIR=include",
            f"-DMIMIR_VERSION_INFO={__version__}",
            f"-DCMAKE_BUILD_TYPE={build_type}",  # not used on MSVC, but no harm
            f"-DCMAKE_PREFIX_PATH={str(dependency_prefix)}",
            f"-DPython_EXECUTABLE={sys.executable}"
        ]

        subprocess.run(
            ["cmake", "-S", ext.sourcedir, "-B", f"{str(temp_directory / 'build')}"] + cmake_args, cwd=str(temp_directory), check=True
        )

        subprocess.run(
            ["cmake", "--build", f"{str(temp_directory / 'build')}", f"-j{multiprocessing.cpu_count()}"], cwd=str(temp_directory), check=True
        )

        install_cmd = ["cmake", "--install", f"{str(temp_directory / 'build')}", "--prefix", f"{str(output_directory / 'pymimir')}"]
        # Reduce wheel size: strip debug symbols from installed binaries when supported.
        # (Manylinux builds often inject -g via environment CFLAGS/CXXFLAGS.)
        if build_type != "Debug" and os.name != "nt":
            install_cmd.append("--strip")

        subprocess.run(install_cmd, check=True)


# The information here can also be placed in setup.cfg - better separation of
# logic and declaration, and simpler if you include description/version in a file.
setup(
    name="pymimir",
    version=__version__,
    author="Simon Stahlberg, Dominik Drexler",
    author_email="simon.stahlberg@gmail.com, dominik.drexler@liu.se",
    url="https://github.com/maichmueller/mimir",
    description="Mimir planning library",
    long_description="",
    python_requires=">=3.12",
    classifiers=[
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
        "Programming Language :: Python :: 3.14",
        "Programming Language :: Python :: Implementation :: CPython",
    ],
    install_requires=[],
    packages=find_packages(where="python/src"),
    package_dir={"": "python/src"},
    ext_modules=[CMakeExtension("pymimir")],
    cmdclass={"build_ext": CMakeBuild},
    zip_safe=False,
    extras_require={
        "test": [
            "pytest",
        ],
    }
)
