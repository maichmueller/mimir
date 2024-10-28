import os
import re
import subprocess
import sys
import argparse
from conan.api.conan_api import ConanAPI
from conan.api.model import Remote, LOCAL_RECIPES_INDEX
from conan.errors import ConanException
import sys
import platform
import packaging._musllinux
from packaging.version import Version


def get_libc_info():
    name, ver = platform.libc_ver()
    if not name:
        musl_ver = packaging._musllinux._get_musl_version(sys.executable)
        if musl_ver:
            name, ver = "musl", f"{musl_ver.major}.{musl_ver.minor}"
    return name, ver


def setup_default_profile(conan_cmd: str, **kwargs):
    subprocess.run([conan_cmd, "profile", "detect", "--force"], check=False)


def set_conan_home(**kwargs):
    homepath = os.getenv("CONAN_HOME")
    is_manylinux = "linux" in sys.platform.lower()
    if is_manylinux:
        # we are inside a container
        # cibuildwheel mounts the host filesystem under /host, so every path has to be relative to that
        os.environ["CONAN_HOME"] = os.path.join("/host", os.path.relpath(homepath, "/"))
    else:
        print("Not inside linux container. Skipping CONAN_HOME setup.")


def verify_libc_compatible_cache(conan_cmd, **kwargs):
    is_manylinux = "linux" in sys.platform.lower()
    if is_manylinux:
        libc_info = get_libc_info()
        libc_id_path = os.path.join(
            "/host", os.path.relpath(os.getenv("LIBC_CACHE_ID_FILE"), "/")
        )
        with open(libc_id_path, "r") as f:
            libc_id = tuple(l.strip() for l in f.readlines())
        print("libc information conan cache:", libc_id)
        print("libc information platform:   ", libc_info)
        if libc_id[0] != libc_info[0] or Version(libc_info[1]) < Version(libc_id[1]):
            print("libc information mismatch. Deleting existing cache.")
            subprocess.run([conan_cmd, "remove", "*", "--confirm"], check=True)
            subprocess.run([conan_cmd, "cache", "clean"], check=True)
        else:
            print("libc information match. Continuing with cache.")
        with open(libc_id_path, "w") as f:
            f.write(f"{libc_info[0]}\n{libc_info[1]}")
    else:
        print("Not on linux. Skipping libc cache check.")


def local_repo_config(**kwargs):
    # Initialize Conan API
    conan = ConanAPI()
    conan_remote_path = os.getenv("CONAN_CENTER_CLONE_PATH")
    try:
        # Remove the conancenter remote
        conan.remotes.get("conancenter")
        conan.remotes.remove("conancenter")
        print("Removed conancenter remote.")
    except ConanException as e:
        print(f"Exception: {e}.")
        print(
            "Perhaps it has already been removed. Continuing without removing the conancenter remote."
        )

    # Add new local_cache remote
    pathed_conan_remote = os.path.join("/host", os.path.relpath(conan_remote_path, "/"))
    new_remote_name = "local_conancenter"
    try:
        remote = conan.remotes.get(new_remote_name)
        if remote.remote_type != LOCAL_RECIPES_INDEX:
            print(
                f"Remote {new_remote_name} already exists and is not of type LOCAL_RECIPES_INDEX."
            )
            print(f"Removing remote {new_remote_name}.")
            conan.remotes.remove(new_remote_name)
            raise ConanException(
                "Remote already exists but is not of type LOCAL_RECIPES_INDEX."
            )
        else:
            print(
                f"Remote {new_remote_name} already exists and is a LOCAL_RECIPES_INDEX-type remote."
            )
    except ConanException as e:
        print(f"Exception: {e}.")
        print(
            f"Remote {new_remote_name} does not exist. Adding new remote {new_remote_name}."
        )
        print(f'Setting new remote {new_remote_name = } at "{pathed_conan_remote}"')
        new_remote = Remote(
            new_remote_name, pathed_conan_remote, remote_type=LOCAL_RECIPES_INDEX
        )
        conan.remotes.add(new_remote, force=True)


def main(conan_cmd, **kwargs):
    sys.stdout.reconfigure(line_buffering=True)
    is_manylinux = "linux" in sys.platform.lower()
    if is_manylinux:
        set_conan_home(**kwargs)
    setup_default_profile(conan_cmd=conan_cmd, **kwargs)
    if is_manylinux:
        verify_libc_compatible_cache(conan_cmd=conan_cmd, **kwargs)
        local_repo_config(conan_cmd=conan_cmd)
