set(MIMIR_LOKI_PATCH_DIR "${CMAKE_CURRENT_LIST_DIR}/../dependencies/loki/patches")
set(MIMIR_LOKI_MULTILEVEL_PATCH "${MIMIR_LOKI_PATCH_DIR}/0001-indexed-hash-set-multi-level-chaining.patch")
set(MIMIR_LOKI_MULTILEVEL_SENTINEL "LOKI_INDEXED_HASH_SET_SUPPORTS_MULTI_LEVEL_CHAINING")
set(MIMIR_LOKI_INDEXED_HASH_SET_RELPATH "include/loki/details/utils/indexed_hash_set.hpp")

# Applies the multi-level chaining patch to the loki tree rooted at ${root} (a directory holding
# include/loki/...). Idempotent: a tree already carrying the sentinel is left alone, so this is
# safe to call on every configure. Sets ${out_var} to TRUE if the tree ends up patched.
#
# See dependencies/loki/CMakeLists.txt for what the patch does and why.
function(mimir_patch_loki_tree root what out_var)
    set(${out_var} FALSE PARENT_SCOPE)

    set(header "${root}/${MIMIR_LOKI_INDEXED_HASH_SET_RELPATH}")
    if(NOT EXISTS "${header}")
        return()
    endif()

    file(READ "${header}" header_contents)
    if(header_contents MATCHES "${MIMIR_LOKI_MULTILEVEL_SENTINEL}")
        set(${out_var} TRUE PARENT_SCOPE)
        return()
    endif()

    find_package(Git REQUIRED)

    # A dependency tree that is itself inside a git work tree (dependencies/installs is, being
    # under the Mimir checkout) makes `git apply` resolve patch paths against *that* repository's
    # root and silently *skip* -- with exit status 0 -- every path outside the current directory.
    # Capping the repository search just above ${root} makes git either find no repository at all
    # (installed trees) or find the one rooted at ${root} (the loki source checkout); both resolve
    # the patch paths against ${root}, which is what we mean.
    get_filename_component(root_parent "${root}" DIRECTORY)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env "GIT_CEILING_DIRECTORIES=${root_parent}"
                ${GIT_EXECUTABLE} -C "${root}" apply "${MIMIR_LOKI_MULTILEVEL_PATCH}"
        RESULT_VARIABLE patch_result
        ERROR_VARIABLE patch_error)

    # Re-read rather than trusting the status, for the silent-skip reason above.
    file(READ "${header}" header_contents)
    if(NOT patch_result EQUAL 0 OR NOT header_contents MATCHES "${MIMIR_LOKI_MULTILEVEL_SENTINEL}")
        message(FATAL_ERROR
            "Failed to apply the loki multi-level chaining patch to ${what} (${root}):\n${patch_error}\n"
            "Wipe dependencies/installs and rebuild the dependencies, or configure with "
            "-DMIMIR_PATCH_LOKI_MULTILEVEL_CHAINING=OFF to build without lifted grounding overlays.")
    endif()
    message(STATUS "Patched ${what}: loki IndexedHashSet multi-level chaining")
    set(${out_var} TRUE PARENT_SCOPE)
endfunction()

macro(configure_loki)
    set(LOKI_MIN_VERSION "0.0.1")

    # `ProblemImpl::create_grounding_overlay` parents a problem's repositories on another
    # problem's, which makes the chain domain -> problem -> overlay three levels deep. Stock loki
    # throws on that. The patch is applied to the loki source by the dependencies superbuild, but
    # that superbuild skips any dependency whose package is already installed -- so once
    # dependencies/installs is populated, the loki ExternalProject and its patch step are never
    # created again and a newly added patch would silently never reach the headers we compile
    # against. Verify (and if necessary fix) the installed headers here, where the check always
    # runs. Doing nothing but reading a header on an up-to-date tree.
    option(MIMIR_PATCH_LOKI_MULTILEVEL_CHAINING
           "Patch loki's IndexedHashSet to support multi-level parent chaining" ON)

    if(MIMIR_PATCH_LOKI_MULTILEVEL_CHAINING)
        # Only ever rewrite headers Mimir owns. A prefix path may well point at a shared loki --
        # a conda environment, /usr/local, another project's install tree -- and editing that from
        # inside someone's build of *this* project is not ours to do. Foreign prefixes are reported
        # and left alone.
        get_filename_component(_mimir_owned_root "${CMAKE_CURRENT_LIST_DIR}/../dependencies" REALPATH)

        set(_mimir_loki_patched FALSE)
        set(_mimir_foreign_loki "")
        foreach(_prefix ${CMAKE_PREFIX_PATH})
            get_filename_component(_prefix_real "${_prefix}" REALPATH)
            string(FIND "${_prefix_real}" "${_mimir_owned_root}/" _owned_position)

            if(_owned_position EQUAL 0)
                mimir_patch_loki_tree("${_prefix}" "installed loki headers" _prefix_patched)
                if(_prefix_patched)
                    set(_mimir_loki_patched TRUE)
                endif()
            elseif(EXISTS "${_prefix}/${MIMIR_LOKI_INDEXED_HASH_SET_RELPATH}")
                file(READ "${_prefix}/${MIMIR_LOKI_INDEXED_HASH_SET_RELPATH}" _prefix_header)
                if(_prefix_header MATCHES "${MIMIR_LOKI_MULTILEVEL_SENTINEL}")
                    set(_mimir_loki_patched TRUE)  ##< already carries the patch; nothing to do
                else()
                    list(APPEND _mimir_foreign_loki "${_prefix}")
                endif()
            endif()
        endforeach()

        if(_mimir_foreign_loki)
            message(WARNING
                "Found loki headers outside Mimir's dependencies tree (${_mimir_foreign_loki}) that do not support "
                "multi-level IndexedHashSet chaining. They were left untouched -- Mimir does not modify installs it "
                "does not own. If one of them is what Mimir compiles against, lifted grounding overlays will throw at "
                "runtime; apply dependencies/loki/patches/0001-indexed-hash-set-multi-level-chaining.patch to it "
                "yourself, or drop it from CMAKE_PREFIX_PATH.")
        endif()

        if(NOT _mimir_loki_patched)
            message(WARNING
                "Could not locate ${MIMIR_LOKI_INDEXED_HASH_SET_RELPATH} under CMAKE_PREFIX_PATH "
                "(${CMAKE_PREFIX_PATH}); lifted grounding overlays need loki's IndexedHashSet to "
                "support multi-level chaining and the installed copy could not be verified.")
        endif()
    endif()
endmacro()
