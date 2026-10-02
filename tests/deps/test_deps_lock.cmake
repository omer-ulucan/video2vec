# SPDX-FileCopyrightText: 2025-2026 Ömer Ulucan
#
# SPDX-License-Identifier: Apache-2.0 OR MIT

# Tests cmake/DepsLock.cmake: FetchContent pins and dependency versions are read
# from deps.lock, a missing or malformed entry stops configuration instead of
# downloading an unpinned file, and an archive that does not match its pin
# fails the configure step. Run with `cmake -P`. Each failure case runs in a
# child CMake process, because a FATAL_ERROR ends the process that raises it.
cmake_minimum_required(VERSION 3.25)

get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(module "${repo_root}/cmake/DepsLock.cmake")

# Child mode: look up one entry and print it.
if(DEFINED CASE_NAME)
    include("${module}")
    video2vec_locked_dep("${CASE_NAME}" url sha256)
    video2vec_locked_version("${CASE_NAME}" version)
    message(STATUS "url=${url}")
    message(STATUS "sha256=${sha256}")
    message(STATUS "version=${version}")
    return()
endif()

# Driver mode.
set(failures 0)
set(work "${CMAKE_CURRENT_BINARY_DIR}/deps_lock_cmake_test")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")
set(good_sha "c73363397f96eb1295602bf44d708a994ad42046c791bf03ea0505d829bdb6a7")
set(other_sha "be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21")
# goodXdep comes first and would match a regular-expression lookup of good.dep.
file(WRITE "${work}/deps.lock"
"# test lock
goodXdep  9.9  ${other_sha}  https://example.invalid/decoy.tar.gz
good.dep  1.0  ${good_sha}  https://example.invalid/good.tar.gz
short     1.0  abc123  https://example.invalid/short.tar.gz
upper     1.0  C73363397F96EB1295602BF44D708A994AD42046C791BF03EA0505D829BDB6A7  https://e.invalid/u
truncated 1.0
")

# record(<label> <ok-var> <output>): reports one check and counts failures in
# the caller's scope.
macro(record label ok_var output)
    if(${ok_var})
        message(STATUS "ok    ${label}")
    else()
        message(STATUS "FAIL  ${label}\n${output}")
        math(EXPR failures "${failures} + 1")
    endif()
endmacro()

# outcome_ok(<rc> <expect-success> <text> <regex> <out-var>): TRUE if the exit
# code matches the expectation and <text> matches <regex>.
function(outcome_ok rc expect_ok text pattern out)
    set(ok FALSE)
    if((expect_ok AND rc EQUAL 0) OR (NOT expect_ok AND NOT rc EQUAL 0))
        if("${text}" MATCHES "${pattern}")
            set(ok TRUE)
        endif()
    endif()
    set(${out} ${ok} PARENT_SCOPE)
endfunction()

# run_case(<label> <lock> <name> <expect-success> <regex>): looks up <name> in a
# child process.
macro(run_case label lock name expect_ok pattern)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -DVIDEO2VEC_DEPS_LOCK=${lock} -DCASE_NAME=${name}
                -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    outcome_ok("${_rc}" ${expect_ok} "${_out}${_err}" "${pattern}" _ok)
    record("${label}" _ok "exit ${_rc}: ${_out}${_err}")
endmacro()

# Lookup returns the entry's URL, hash and version.
run_case("url lookup" "${work}/deps.lock" good.dep TRUE "url=https://example.invalid/good.tar.gz")
run_case("sha256 lookup" "${work}/deps.lock" good.dep TRUE "sha256=${good_sha}")
run_case("version lookup" "${work}/deps.lock" good.dep TRUE "version=1.0")
# A dot in a name is literal: good.dep does not resolve to the decoy before it.
run_case("dot in a name is literal" "${work}/deps.lock" good.dep TRUE "sha256=${good_sha}")
# An unknown name stops configuration.
run_case("unknown name" "${work}/deps.lock" missing FALSE "no entry 'missing'")
# Hashes must be exactly 64 lowercase hex digits.
run_case("short hash rejected" "${work}/deps.lock" short FALSE "no valid sha256")
run_case("uppercase hash rejected" "${work}/deps.lock" upper FALSE "no valid sha256")
# A line without all four fields stops configuration.
run_case("malformed line" "${work}/deps.lock" truncated FALSE "malformed entry 'truncated'")
# A missing lock file stops configuration.
run_case("missing lock file" "${work}/none.lock" good.dep FALSE "deps.lock not found")

# The repository's lock pins every FetchContent fallback and every unpacked
# dependency with a real hash.
foreach(dep IN ITEMS spdlog nlohmann_json yaml-cpp cxxopts googletest benchmark
                     onnxruntime tesseract leptonica)
    run_case("repository pin: ${dep}" "${repo_root}/deps.lock" ${dep} TRUE "sha256=[0-9a-f]")
endforeach()
file(STRINGS "${repo_root}/deps.lock" repo_lines REGEX "^[^#]" ENCODING UTF-8)
set(placeholder FALSE)
set(not_https "")
foreach(line IN LISTS repo_lines)
    if(line MATCHES "[ \t]0000000000000000000000000000000000000000000000000000000000000000[ \t]")
        set(placeholder TRUE)
    endif()
    if(NOT line MATCHES "[ \t]https://[^ \t]+[ \t]*$")
        string(APPEND not_https "${line}\n")
    endif()
endforeach()
# Placeholder hashes never reach the repository lock.
if(placeholder)
    set(_ok FALSE)
else()
    set(_ok TRUE)
endif()
record("no placeholder hashes in the repository lock" _ok "found a zero hash")
# The repository lock downloads over HTTPS only (deps_fetch also accepts
# file:// for the tests).
if(not_https STREQUAL "")
    set(_ok TRUE)
else()
    set(_ok FALSE)
endif()
record("repository lock is HTTPS-only" _ok "${not_https}")
# CMake lists split on ';' and file(STRINGS) merges lines after an unbalanced
# '[', so the lock must not contain those characters anywhere.
file(READ "${repo_root}/deps.lock" lock_text)
if(lock_text MATCHES "[];[]")
    set(_ok FALSE)
else()
    set(_ok TRUE)
endif()
record("repository lock has no list or bracket characters" _ok "found ; [ or ]")

# End to end: a project that declares a fallback through
# video2vec_fetchcontent_declare() configures when the archive matches its pin
# and fails when it does not.
file(MAKE_DIRECTORY "${work}/payload/pkg")
file(WRITE "${work}/payload/pkg/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.25)\nproject(pkg NONE)\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar czf "${work}/pkg.tar.gz" pkg
    WORKING_DIRECTORY "${work}/payload")
file(SHA256 "${work}/pkg.tar.gz" pkg_sha)
file(WRITE "${work}/pinned.lock" "pkg 1.0 ${pkg_sha} file://${work}/pkg.tar.gz\n")
file(WRITE "${work}/tampered.lock" "pkg 1.0 ${good_sha} file://${work}/pkg.tar.gz\n")
file(MAKE_DIRECTORY "${work}/consumer")
file(WRITE "${work}/consumer/CMakeLists.txt" "cmake_minimum_required(VERSION 3.25)
project(consumer NONE)
include(\"${module}\")
video2vec_fetchcontent_declare(pkg)
FetchContent_MakeAvailable(pkg)
")

# configure_case(<label> <lock> <expect-success> <regex>): configures the
# consumer project against <lock>.
macro(configure_case label lock expect_ok pattern)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${work}/consumer" -B "${work}/build-${label}"
                -DVIDEO2VEC_DEPS_LOCK=${lock}
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    outcome_ok("${_rc}" ${expect_ok} "${_out}${_err}" "${pattern}" _ok)
    record("${label}" _ok "exit ${_rc}: ${_out}${_err}")
endmacro()

# The archive that matches its pin is fetched and configured.
configure_case(fetch-pinned "${work}/pinned.lock" TRUE "Configuring done")
# The same archive under another pin is rejected: the build cannot start.
configure_case(fetch-tampered "${work}/tampered.lock" FALSE "([Mm]ismatch|does not match)")

file(REMOVE_RECURSE "${work}")
if(failures GREATER 0)
    message(FATAL_ERROR "${failures} check(s) failed")
endif()
message(STATUS "all checks passed")
