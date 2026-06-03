#include "video2vec/windowing/windowing.hpp"
#include <algorithm>

namespace video2vec::windowing {

std::vector<Window> generate_windows(int64_t total_duration_ms, const WindowingConfig& config) {
    std::vector<Window> windows;
    if (total_duration_ms <= 0 || config.window_duration_ms <= 0) return windows;
    int64_t step = config.window_duration_ms - config.overlap_ms;
    if (step <= 0) step = config.window_duration_ms;
    int index = 0;
    for (int64_t start = 0; start < total_duration_ms; start += step) {
        Window w{};
        w.t0_ms = start;
        w.t1_ms = std::min(start + config.window_duration_ms, total_duration_ms);
        w.index = index++;
        windows.push_back(w);
        if (w.t1_ms >= total_duration_ms) break;
    }
    return windows;
}

} // namespace video2vec::windowing
