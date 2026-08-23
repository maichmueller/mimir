# Clamp gtl's destructive-interference padding to at most 128 bytes.
#
# Why: gtl pads each shard of `parallel_flat_hash_set` with
#   struct alignas(gtl_hardware_destructive_interference_size) Inner : public Lockable
# (include/gtl/phmap.hpp) so that two shards' locks never share a cache line. The padding value comes
# from std::hardware_destructive_interference_size when the toolchain exposes it
# (include/gtl/gtl_config.hpp), otherwise from a hardcoded 64.
#
# Some toolchains report 256 there (Apple clang on arm64 defines __GCC_DESTRUCTIVE_SIZE=256). That
# alignment propagates by containment: gtl Inner -> gtl::parallel_flat_hash_set ->
# valla::IndexedHashSet (held by value) -> mimir::formalism::ProblemImpl. nanobind stores a bound
# type's alignment in an 8-bit bitfield (`uint32_t align : 8`) and asserts
# `alignof(T) < (1 << 8)`, so a 256-byte-aligned ProblemImpl fails to compile in the Python bindings.
# That assert is unchanged from nanobind v2.7.0 through v2.13.0 and current master, so upgrading
# nanobind does not help; the alignment itself has to come down.
#
# This clamps to 128 rather than gtl's 64 fallback because 128 is the actual cache line size on
# arm64 -- dropping to 64 would under-align relative to the real line and could reintroduce the false
# sharing the padding exists to prevent. Mimir's parallel beam search is exactly the concurrent
# workload these sharded maps serve, so that distinction matters here.
#
# Deliberately gated on the *value*, not on the platform: this is a no-op anywhere the reported size
# is already <= 128 (e.g. x86_64, where it is 64), so it needs no per-OS branch and cannot regress
# those targets. Correctness is unaffected on any platform -- this only controls padding.
#
# Idempotent: after substitution the searched-for text no longer occurs (the macro body becomes
# `(std::...` rather than `std::...`), so re-running the patch step is a no-op.

if (NOT DEFINED GTL_CONFIG_HPP)
    message(FATAL_ERROR "patch_interference_size.cmake requires -DGTL_CONFIG_HPP=<path>")
endif()

if (NOT EXISTS "${GTL_CONFIG_HPP}")
    message(FATAL_ERROR "patch_interference_size.cmake: file not found: ${GTL_CONFIG_HPP}")
endif()

set(_original "#define gtl_hardware_destructive_interference_size std::hardware_destructive_interference_size")
set(_clamped  "#define gtl_hardware_destructive_interference_size (std::hardware_destructive_interference_size > 128 ? 128 : std::hardware_destructive_interference_size)")

file(READ "${GTL_CONFIG_HPP}" _content)

string(FIND "${_content}" "${_original}" _match_pos)
if (_match_pos EQUAL -1)
    string(FIND "${_content}" "${_clamped}" _already_pos)
    if (_already_pos EQUAL -1)
        message(FATAL_ERROR
            "patch_interference_size.cmake: expected macro definition not found in ${GTL_CONFIG_HPP}. "
            "The pinned gtl revision likely changed -- re-check gtl_config.hpp before bumping GIT_TAG.")
    endif()
    message(STATUS "gtl destructive-interference clamp already applied; nothing to do.")
    return()
endif()

string(REPLACE "${_original}" "${_clamped}" _content "${_content}")
file(WRITE "${GTL_CONFIG_HPP}" "${_content}")

message(STATUS "Patched gtl: clamped destructive-interference padding to <= 128 bytes (nanobind requires alignof < 256).")
