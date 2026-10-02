# SPDX-FileCopyrightText: 2025-2026 Ömer Ulucan
#
# SPDX-License-Identifier: Apache-2.0 OR MIT

# Tests cmake/DepsLock.cmake: FetchContent pins are read from deps.lock, and a
# missing or malformed entry stops configuration instead of downloading an
# unpinned file. Run with `cmake -P`. Each failure case runs in a child CMake
# process, because a FATAL_ERROR ends the process that raises it.
cmake_minimum_required(VERSION 3.25)

get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(module "${repo_root}/cmake/DepsLock.cmake")

# Child mode: look up one entry and print it.
if(DEFINED CASE_NAME)
    include("${module}")
    video2vec_locked_dep("${CASE_NAME}" url sha256)
    message(STATUS "url=${url}")
    message(STATUS "sha256=${sha256}")
    return()
endif()

# Driver mode.
set(failures 0)
set(work "${CMAKE_CURRENT_BINARY_DIR}/deps_lock_cmake_test")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")
set(good_sha "c73363397f96eb1295602bf44d708a994ad42046c791bf03ea0505d829bdb6a7")
file(WRITE "${work}/deps.lock"
"# test lock
good.dep  1.0  ${good_sha}  https://example.invalid/good.tar.gz
short     1.0  abc123  https://example.invalid/short.tar.gz
upper     1.0  C73363397F96EB1295602BF44D708A994AD42046C791BF03EA0505D829BDB6A7  https://e.invalid/u
truncated 1.0
")

# run_case(<label> <lock> <name> <expect-success> <expected-output-regex>)
function(run_case label lock name expect_ok pattern)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -DVIDEO2VEC_DEPS_LOCK=${lock} -DCASE_NAME=${name}
                -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(ok FALSE)
    if(expect_ok AND rc EQUAL 0)
        set(ok TRUE)
    elseif(NOT expect_ok AND NOT rc EQUAL 0)
        set(ok TRUE)
    endif()
    if(ok AND NOT "${out}${err}" MATCHES "${pattern}")
        set(ok FALSE)
    endif()
    if(ok)
        message(STATUS "ok    ${label}")
    else()
        message(STATUS "FAIL  ${label} (exit ${rc})\n${out}${err}")
        math(EXPR n "${failures} + 1")
        set(failures ${n} PARENT_SCOPE)
    endif()
endfunction()

run_case("url lookup" "${work}/deps.lock" good.dep TRUE "url=https://example.invalid/good.tar.gz")
run_case("sha256 lookup" "${work}/deps.lock" good.dep TRUE "sha256=${good_sha}")
run_case("dot in a name is literal" "${work}/deps.lock" goodXdep FALSE "no entry 'goodXdep'")
run_case("unknown name" "${work}/deps.lock" missing FALSE "no entry 'missing'")
run_case("short hash rejected" "${work}/deps.lock" short FALSE "no valid sha256")
run_case("uppercase hash rejected" "${work}/deps.lock" upper FALSE "no valid sha256")
run_case("malformed line" "${work}/deps.lock" truncated FALSE "malformed entry 'truncated'")
run_case("missing lock file" "${work}/none.lock" good.dep FALSE "deps.lock not found")

# The repository's lock must pin every FetchContent fallback the build uses,
# with a real hash rather than a placeholder.
foreach(dep IN ITEMS spdlog nlohmann_json yaml-cpp cxxopts googletest benchmark)
    run_case("repository pin: ${dep}" "${repo_root}/deps.lock" ${dep} TRUE "sha256=[0-9a-f]")
endforeach()
file(READ "${repo_root}/deps.lock" lock_text)
if(lock_text MATCHES "[ \t]0000000000000000000000000000000000000000000000000000000000000000[ \t]")
    message(STATUS "FAIL  repository lock contains a placeholder hash")
    math(EXPR failures "${failures} + 1")
else()
    message(STATUS "ok    no placeholder hashes in the repository lock")
endif()

file(REMOVE_RECURSE "${work}")
if(failures GREATER 0)
    message(FATAL_ERROR "${failures} check(s) failed")
endif()
message(STATUS "all checks passed")
