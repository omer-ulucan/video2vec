if(BUILD_BENCHMARKS)
    find_package(benchmark QUIET)
    if(NOT benchmark_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            benchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG v1.8.3
        )
        FetchContent_MakeAvailable(benchmark)
    endif()
    add_executable(bench_core bench_core.cpp)
    target_link_libraries(bench_core PRIVATE video2vec::core video2vec::windowing video2vec::vision benchmark::benchmark_main)
endif()
