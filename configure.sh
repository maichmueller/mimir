#!/bin/bash

use_conan=true
deps_policy=missing
only_install=false
conan_cmd=conan
cmake_build_folder=build
cmake_source_folder=.
config=Release
cmake_cmd=cmake
cmake_extra_args=()
# Define valid CMake build types
valid_build_types=("Debug" "Release" "RelWithDebInfo" "MinSizeRel")

# Loop through all arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
  "--noconan")
    use_conan=false
    ;;
  "--only_install")
    only_install=true
    ;;
  "--deps_policy="*)
    deps_policy="${1#*=}"
    ;;
  "--deps_policy")
    deps_policy="$2"
    shift # Move to the next argument
    ;;
  "--cmake_cmd="*)
      cmake_cmd="${1#*=}"
      ;;
  "--cmake_cmd")
      cmake_cmd="$2"
      shift # Move to the next argument
      ;;
  "--conan_cmd="*)
      conan_cmd="${1#*=}"
      ;;
  "--conan_cmd")
      conan_cmd="$2"
      shift # Move to the next argument
      ;;
  "--build="*)
    cmake_build_folder="${1#*=}"
    ;;
  "--build")
    cmake_build_folder="$2"
    shift # Move to the next argument
    ;;
  "--source="*)
    cmake_source_folder="${1#*=}"
    ;;
  "--source")
    cmake_source_folder="$2"
    shift # Move to the next argument
    ;;
  "--toolchain_file="*)
    toolchain_file="${1#*=}"
    ;;
  "--toolchain_file")
    toolchain_file="$2"
    shift # Move to the next argument
    ;;
  "--config="*)
    config="${1#*=}"
    ;;
  "--config")
    config="$2"
    shift # Move to the next argument
    ;;

  *)
    # If the argument doesn't match any known options, add it to cmake_extra_args
    cmake_extra_args+=("$1")
    ;;
  esac
  shift # Move to the next argument
done


if [ -e "$cmake_source_folder/conanfile.py" ]; then
  toolchain_file="$cmake_build_folder/conan/build/$config/generators/conan_toolchain.cmake"
else
  toolchain_file="$cmake_build_folder/conan/conan_toolchain.cmake"
fi


# Function to find a near-match in valid build types
find_near_match() {
  local input=$1
  for valid_type in "${valid_build_types[@]}"; do
    if [[ "${valid_type,}" == "${input,}"* ]]; then
      echo "$valid_type"
      return 0
    fi
  done
  return 1
}

# Function to check if a build type is valid
is_valid_build_type() {
  local config=$1
  for valid_type in "${valid_build_types[@]}"; do
    if [ "$config" == "$valid_type" ]; then
      return 0 # Valid build type
    fi
  done
  return 1 # Invalid build type
}

# Check if the provided build type is valid
if is_valid_build_type "$config"; then
  echo "Selected build type: $config"
  # Perform further actions with the valid build type
else
  # Try to find a near-match
  near_match=$(find_near_match "$config")

  if [ -n "$near_match" ]; then
    echo "Did you mean '$near_match'? Auto-adapting..."
    input_build_type=$near_match
    echo "Selected build type: $input_build_type"
    # Perform further actions with the valid build type
  else
    echo "Error: Invalid build type. Valid build types are: ${valid_build_types[*]}"
    exit 1
  fi
fi

script_dir=$(dirname $0)

echo "Configuration script called with the args..."
echo "use_conan: $use_conan"
echo "conan_cmd: $conan_cmd"
echo "cmake_build_folder: $cmake_build_folder"
echo "cmake_source_folder: $cmake_source_folder"
echo "config: $config"
echo "toolchain_file: $toolchain_file"
echo "cmake_extra_args: ${cmake_extra_args[*]}"
echo "Executing cmake config."

if [ "$use_conan" = true ]; then
  # install all dependencies defined for conan first
  conan_args="-s build_type=Release --profile:host=default --profile:build=default --build=\"$deps_policy\""
  action="$conan_cmd create dependencies/loki $conan_args  --options=\"loki/*:fPIC=True\""
  eval "$action"
  action="$conan_cmd create dependencies/nauty $conan_args --options=\"nauty/*:fPIC=True\""
  eval "$action"
  action="conan create dependencies/cista --version=2024.10.22 $conan_args"
  eval "$action"
  action="$conan_cmd install . -of=$cmake_build_folder/conan -g CMakeDeps -s \"&\":build_type=$config $conan_args"
  eval "$action"
  # append the necessary cmake config to the cmake call
  cmake_extra_args=("${cmake_extra_args[@]}" \
  -DCMAKE_TOOLCHAIN_FILE="$toolchain_file" \
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW)
fi


if [ "$only_install" = true ]; then
  echo "Only installing dependencies. Exiting."
  exit 0
fi
cmake_run="${cmake_cmd} \
  -S $cmake_source_folder \
  -B $cmake_build_folder \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=$config \
  -DCONAN_COMMAND=$conan_cmd \
  ${cmake_extra_args[*]}"

echo "Executing command: $cmake_run"
$cmake_run
