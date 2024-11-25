#!/usr/bin/bash

conan_cmd=conan
# Loop through all arguments
while [[ $# -gt 0 ]]; do
  case "$1" in
  "--conan_cmd="*)
      conan_cmd="${1#*=}"
      ;;
  "--conan_cmd")
      conan_cmd="$2"
      shift # Move to the next argument
      ;;
  esac
  shift # Move to the next argument
done


$conan_cmd export dependencies/loki --version=0.0.7
$conan_cmd export dependencies/nauty --version=2.8.8
$conan_cmd export dependencies/cista --version=2024.10.22
$conan_cmd export dependencies/unordered_dense_cista --version=2024.10.22
