# SPDX-FileCopyrightText: 2025-2026 Ömer Ulucan
#
# SPDX-License-Identifier: Apache-2.0 OR MIT

if(BUILD_BENCHMARKS)
    find_package(benchmark QUIET)
    if(NOT benchmark_FOUND)
        # Without these, benchmark's own build would try to fetch GoogleTest
        # outside deps.lock, and would build its own tests.
        set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_DOWNLOAD_DEPENDENCIES OFF CACHE BOOL "" FORCE)
        video2vec_fetchcontent_declare(benchmark)
        FetchContent_MakeAvailable(benchmark)
    endif()
    add_executable(bench_core bench_core.cpp)
    target_link_libraries(bench_core PRIVATE video2vec::core video2vec::windowing video2vec::vision benchmark::benchmark_main)
endif()
