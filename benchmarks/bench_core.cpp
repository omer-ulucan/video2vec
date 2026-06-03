#include <benchmark/benchmark.h>
#include <video2vec/core/thread_pool.hpp>
#include <video2vec/windowing/windowing.hpp>
#include <video2vec/vision/frame_selector.hpp>
#include <vector>

using namespace video2vec;

static void BM_WindowGeneration(benchmark::State& state) {
    windowing::WindowingConfig cfg{45000, 5000};
    for (auto _ : state) {
        auto windows = windowing::generate_windows(state.range(0), cfg);
        benchmark::DoNotOptimize(windows);
    }
}
BENCHMARK(BM_WindowGeneration)->Arg(3600000)->Arg(7200000);

static void BM_FrameSelection(benchmark::State& state) {
    std::vector<vision::Frame> frames;
    for (int i = 0; i < state.range(0); ++i) {
        vision::Frame f{};
        f.pts_ms = i * 1000; f.width = 640; f.height = 480;
        f.rgb_data.resize(640 * 480 * 3, static_cast<uint8_t>(i % 256));
        f.score = static_cast<double>(i);
        frames.push_back(f);
    }
    vision::SelectionConfig cfg{6, 4, 0.25, 1.0, 1.0, 0.99, 0.995};
    for (auto _ : state) {
        auto selected = vision::select_frames(frames, cfg);
        benchmark::DoNotOptimize(selected);
    }
}
BENCHMARK(BM_FrameSelection)->Arg(30)->Arg(60)->Arg(120);

static void BM_ThreadPool(benchmark::State& state) {
    core::ThreadPool pool(4);
    for (auto _ : state) {
        std::vector<std::future<int>> futures;
        for (int i = 0; i < state.range(0); ++i) futures.push_back(pool.submit([]() { return 42; }));
        for (auto& f : futures) benchmark::DoNotOptimize(f.get());
    }
}
BENCHMARK(BM_ThreadPool)->Arg(10)->Arg(100)->Arg(1000);

BENCHMARK_MAIN();
