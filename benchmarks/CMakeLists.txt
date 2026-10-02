# SPDX-FileCopyrightText: 2025-2026 Ömer Ulucan
#
# SPDX-License-Identifier: Apache-2.0 OR MIT

if(BUILD_BENCHMARKS)
    find_package(benchmark QUIET)
    if(NOT benchmark_FOUND)
        include(FetchContent)
        video2vec_locked_dep(benchmark _url _sha256)
        FetchContent_Declare(benchmark URL "${_url}" URL_HASH SHA256=${_sha256})
        FetchContent_MakeAvailable(benchmark)
    endif()
    add_executable(bench_core bench_core.cpp)
    target_link_libraries(bench_core PRIVATE video2vec::core video2vec::windowing video2vec::vision benchmark::benchmark_main)
endif()
