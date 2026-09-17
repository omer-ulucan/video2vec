// Demonstrates the library pieces that need no media file or model:
// windowing, frame scoring/selection per window, packaging and the thread pool.
#include <video2vec/core/logger.hpp>
#include <video2vec/core/thread_pool.hpp>
#include <video2vec/windowing/windowing.hpp>
#include <video2vec/vision/frame_selector.hpp>
#include <video2vec/flatbuffers/packager.hpp>

#include <cstdint>
#include <future>
#include <iostream>
#include <vector>

int main() {
    using namespace video2vec;
    core::Logger::initialize("basic_pipeline");

    windowing::WindowingConfig cfg{};
    cfg.window_duration_ms = 45000;
    cfg.overlap_ms = 5000;
    if (auto v = windowing::validate_config(cfg); !v) {
        std::cerr << v.error().message << "\n";
        return 1;
    }
    const int64_t duration_ms = 120000;
    auto windows = windowing::generate_windows(duration_ms, cfg);
    std::cout << "Generated " << windows.size() << " windows\n";

    // Synthetic 64x48 frames, one per second, with increasing texture so the
    // selector has something to rank.
    std::vector<vision::Frame> frames;
    for (int i = 0; i < 120; ++i) {
        vision::Frame f{};
        f.pts_ms = i * 1000;
        f.width = 64;
        f.height = 48;
        f.rgb_data.resize(64 * 48 * 3);
        for (size_t p = 0; p < f.rgb_data.size(); ++p) f.rgb_data[p] = static_cast<uint8_t>((p * (i + 1)) % 251);
        frames.push_back(std::move(f));
    }

    auto by_window = windowing::assign_to_windows<vision::Frame>(frames, windows, [](const vision::Frame& f) { return f.pts_ms; });
    vision::SelectionConfig sel_cfg{};
    sel_cfg.max_frames_per_window = 4;
    sel_cfg.min_frames_per_window = 2;

    std::vector<flatbuffers::PackagedWindow> packaged;
    for (size_t i = 0; i < windows.size(); ++i) {
        auto selected = vision::select_frames(by_window[i], sel_cfg);
        flatbuffers::PackagedWindow pw{};
        pw.t0_ms = windows[i].t0_ms;
        pw.t1_ms = windows[i].t1_ms;
        pw.index = windows[i].index;
        for (auto& f : selected) {
            f.rgb_data.clear();  // keep timing/score metadata only
            pw.frames.push_back(std::move(f));
        }
        std::cout << "Window " << pw.index << " [" << pw.t0_ms << ", " << pw.t1_ms << ") selected "
                  << pw.frames.size() << " of " << by_window[i].size() << " frames\n";
        packaged.push_back(std::move(pw));
    }

    auto buffer = flatbuffers::package_windows(packaged);
    if (!buffer) {
        std::cerr << "Packaging failed: " << buffer.error().message << "\n";
        return 1;
    }
    auto unpacked = flatbuffers::unpack_windows(buffer.value());
    std::cout << "Packaged " << buffer.value().size() << " bytes, round-trips " << (unpacked ? unpacked.value().size() : 0) << " windows\n";

    core::ThreadPool pool(2);
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 5; ++i) futures.push_back(pool.submit([i]() { return i * i; }));
    for (auto& f : futures) std::cout << "Future result: " << f.get() << "\n";
    return 0;
}
