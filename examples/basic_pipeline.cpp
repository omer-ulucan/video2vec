#include <video2vec/core/logger.hpp>
#include <video2vec/core/config.hpp>
#include <video2vec/core/thread_pool.hpp>
#include <video2vec/windowing/windowing.hpp>
#include <video2vec/vision/frame_selector.hpp>
#include <video2vec/flatbuffers/packager.hpp>
#include <iostream>
#include <vector>

int main() {
    video2vec::core::Logger::initialize("basic_pipeline");
    video2vec::windowing::WindowingConfig cfg{};
    cfg.window_duration_ms = 45000;
    cfg.overlap_ms = 5000;
    auto windows = video2vec::windowing::generate_windows(120000, cfg);
    std::cout << "Generated " << windows.size() << " windows\n";
    std::vector<video2vec::vision::Frame> frames;
    for (int i = 0; i < 10; ++i) {
        video2vec::vision::Frame f{};
        f.pts_ms = i * 1000; f.width = 640; f.height = 480;
        f.rgb_data.resize(640 * 480 * 3, static_cast<uint8_t>(i * 20));
        f.entropy = static_cast<double>(i); f.score = static_cast<double>(i);
        frames.push_back(f);
    }
    video2vec::vision::SelectionConfig sel_cfg{};
    sel_cfg.max_frames_per_window = 4;
    auto selected = video2vec::vision::select_frames(frames, sel_cfg);
    std::cout << "Selected " << selected.size() << " frames\n";
    std::vector<video2vec::flatbuffers::PackagedWindow> packaged;
    for (const auto& w : windows) {
        video2vec::flatbuffers::PackagedWindow pw{};
        pw.t0_ms = w.t0_ms; pw.t1_ms = w.t1_ms; pw.index = w.index;
        pw.transcript = "Simulated transcript for window " + std::to_string(w.index);
        packaged.push_back(pw);
    }
    auto buffer = video2vec::flatbuffers::package_windows(packaged);
    if (buffer) std::cout << "Packaged " << buffer.value().size() << " bytes\n";
    video2vec::core::ThreadPool pool(2);
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 5; ++i) futures.push_back(pool.submit([i]() { return i * i; }));
    for (auto& f : futures) std::cout << "Future result: " << f.get() << "\n";
    return 0;
}
