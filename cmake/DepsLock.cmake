# SPDX-FileCopyrightText: 2025-2026 Ömer Ulucan
#
# SPDX-License-Identifier: Apache-2.0 OR MIT

# Reads download pins from deps.lock, the file the shell scripts use too, so
# every FetchContent fallback is declared with URL + URL_HASH and CMake checks
# the archive's SHA-256 before extracting it. Format of deps.lock: one entry
# per line, `<name> <version> <sha256> <url>`; '#' starts a comment line.

if(NOT DEFINED VIDEO2VEC_DEPS_LOCK)
    get_filename_component(VIDEO2VEC_DEPS_LOCK "${CMAKE_CURRENT_LIST_DIR}/../deps.lock" ABSOLUTE)
endif()

# video2vec_locked_dep(<name> <url-var> <sha256-var>)
#
# Sets <url-var> and <sha256-var> in the caller's scope from the deps.lock entry
# whose first column is exactly <name>. Configuration stops with FATAL_ERROR if
# the lock file or the entry is missing, the entry does not have four fields,
# or its hash is not 64 lowercase hex digits, so a fallback can never fetch an
# unpinned file.
#
# Example:
#   video2vec_locked_dep(spdlog _url _sha256)
#   FetchContent_Declare(spdlog URL "${_url}" URL_HASH SHA256=${_sha256})
function(video2vec_locked_dep name out_url out_sha256)
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
        list(GET fields 2 sha256)
        list(GET fields 3 url)
        # CMake regular expressions have no {n} repetition, so check the length apart.
        string(LENGTH "${sha256}" sha256_length)
        if(NOT sha256 MATCHES "^[0-9a-f]+$" OR NOT sha256_length EQUAL 64)
            message(FATAL_ERROR "deps.lock: entry '${name}' has no valid sha256 "
                                "(64 lowercase hex digits)")
        endif()
        set(${out_url} "${url}" PARENT_SCOPE)
        set(${out_sha256} "${sha256}" PARENT_SCOPE)
        return()
    endforeach()
    message(FATAL_ERROR "deps.lock: no entry '${name}' in ${VIDEO2VEC_DEPS_LOCK}")
endfunction()
