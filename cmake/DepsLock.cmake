# SPDX-FileCopyrightText: 2025-2026 Ömer Ulucan
#
# SPDX-License-Identifier: Apache-2.0 OR MIT

# Reads download pins from deps.lock, the file the shell scripts use too, so
# every FetchContent fallback is declared with URL + URL_HASH and CMake checks
# the archive's SHA-256 before extracting it, and the locally unpacked
# dependencies are found under directory names that carry the pinned version.
# Format of deps.lock: one entry per line, `<name> <version> <sha256> <url>`;
# '#' starts a comment line. The lock must not contain ';', '[' or ']', which
# CMake's list handling would misread (tests/deps/test_deps_lock.cmake checks).

if(NOT DEFINED VIDEO2VEC_DEPS_LOCK)
    get_filename_component(VIDEO2VEC_DEPS_LOCK "${CMAKE_CURRENT_LIST_DIR}/../deps.lock" ABSOLUTE)
endif()

# _video2vec_lock_entry(<name> <version-var> <sha256-var> <url-var>)
#
# Internal reader: sets the three fields of the entry whose first column is
# exactly <name>. Stops configuration with FATAL_ERROR if the lock file or the
# entry is missing, the entry does not have four fields, or its hash is not
# 64 lowercase hex digits.
function(_video2vec_lock_entry name out_version out_sha256 out_url)
    if(NOT EXISTS "${VIDEO2VEC_DEPS_LOCK}")
        message(FATAL_ERROR "deps.lock not found: ${VIDEO2VEC_DEPS_LOCK}")
    endif()
    file(STRINGS "${VIDEO2VEC_DEPS_LOCK}" lines)
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        if(line STREQUAL "" OR line MATCHES "^#")
            continue()
        endif()
        string(REGEX REPLACE "[ \t]+" ";" fields "${line}")
        list(GET fields 0 entry)
        # STREQUAL, not MATCHES: a dot in a name such as "whisper.cpp" is literal.
        if(NOT entry STREQUAL name)
            continue()
        endif()
        list(LENGTH fields count)
        if(NOT count EQUAL 4)
            message(FATAL_ERROR "deps.lock: malformed entry '${name}' in ${VIDEO2VEC_DEPS_LOCK} "
                                "(expected: name version sha256 url)")
        endif()
        list(GET fields 1 version)
        list(GET fields 2 sha256)
        list(GET fields 3 url)
        # CMake regular expressions have no {n} repetition, so check the length apart.
        string(LENGTH "${sha256}" sha256_length)
        if(NOT sha256 MATCHES "^[0-9a-f]+$" OR NOT sha256_length EQUAL 64)
            message(FATAL_ERROR "deps.lock: entry '${name}' has no valid sha256 "
                                "(64 lowercase hex digits)")
        endif()
        set(${out_version} "${version}" PARENT_SCOPE)
        set(${out_sha256} "${sha256}" PARENT_SCOPE)
        set(${out_url} "${url}" PARENT_SCOPE)
        return()
    endforeach()
    message(FATAL_ERROR "deps.lock: no entry '${name}' in ${VIDEO2VEC_DEPS_LOCK}")
endfunction()

# video2vec_locked_dep(<name> <url-var> <sha256-var>)
#
# Sets <url-var> and <sha256-var> in the caller's scope from the deps.lock entry
# <name>. Errors as for any lock lookup (missing file or entry, malformed line,
# invalid hash): configuration stops, so nothing unpinned is ever fetched.
function(video2vec_locked_dep name out_url out_sha256)
    _video2vec_lock_entry("${name}" version sha256 url)
    set(${out_url} "${url}" PARENT_SCOPE)
    set(${out_sha256} "${sha256}" PARENT_SCOPE)
endfunction()

# video2vec_locked_version(<name> <version-var>)
#
# Sets <version-var> in the caller's scope to the pinned version of <name>, for
# example to find the directory scripts/ci-setup-deps.sh unpacked it into.
# Same errors as video2vec_locked_dep().
function(video2vec_locked_version name out_version)
    _video2vec_lock_entry("${name}" version sha256 url)
    set(${out_version} "${version}" PARENT_SCOPE)
endfunction()

# video2vec_fetchcontent_declare(<name>)
#
# FetchContent_Declare(<name> URL <pinned url> URL_HASH SHA256=<pinned hash>)
# for the deps.lock entry <name>, without leaving lookup variables behind in
# the caller's scope. Follow it with FetchContent_MakeAvailable(<name>).
#
# Example:
#   video2vec_fetchcontent_declare(spdlog)
#   FetchContent_MakeAvailable(spdlog)
function(video2vec_fetchcontent_declare name)
    include(FetchContent)
    _video2vec_lock_entry("${name}" version sha256 url)
    FetchContent_Declare("${name}" URL "${url}" URL_HASH "SHA256=${sha256}")
endfunction()
