#!/usr/bin/python3

import argparse
import os
import subprocess
import yaml

# Parse arguments
parser = argparse.ArgumentParser(description="Export dependencies using Conan.")
parser.add_argument(
    "--conan_cmd",
    type=str,
    default="conan",
    help="Command to invoke Conan (default: 'conan').",
)
args = parser.parse_args()

# Conan command
conan_cmd = args.conan_cmd


# Read versions from conandata.yml
def read_versions_from_conandata(file_path):
    if not os.path.exists(file_path):
        raise FileNotFoundError(f"conandata.yml not found at {file_path}")

    with open(file_path, "r") as f:
        data = yaml.safe_load(f)

    # Extract versions (assuming structure {requirements: {<dep>/<version>}})
    raw_deps = data.get("requirements", [])
    dependencies = dict()
    for dep in raw_deps:
        name, version = dep.split("/")
        dependencies[name] = version
    return dependencies


# Path to conandata.yml (adjust as needed)
conandata_path = "conandata.yml"

# Try to load dependency versions
try:
    dependency_versions = read_versions_from_conandata(conandata_path)
except Exception as e:
    print(f"Error reading conandata.yml: {e}")
    exit(1)

# Define the dependencies to export
dependencies = ["loki", "nauty", "cista", "unordered_dense_cista"]

# Export dependencies
for dep in dependencies:
    version = dependency_versions.get(dep)
    if not version:
        print(f"Warning: Version for dependency '{dep}' not found in conandata.yml.")
        continue

    try:
        subprocess.run(
            [conan_cmd, "export", f"dependencies/{dep}", f"--version={version}"],
            check=True,
        )
        print(f"Successfully exported {dep} version {version}.")
    except subprocess.CalledProcessError as e:
        print(f"Error exporting {dep} version {version}: {e}")
