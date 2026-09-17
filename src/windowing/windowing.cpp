#include "video2vec/windowing/windowing.hpp"
#include <algorithm>
#include <string>

namespace video2vec::windowing {

core::Result<void> validate_config(const WindowingConfig& config) {
    if (config.window_duration_ms <= 0) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::InvalidArgument,
            "window duration must be positive (got " + std::to_string(config.window_duration_ms) + " ms)"));
    }
    if (config.overlap_ms < 0) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::InvalidArgument,
            "window overlap must not be negative (got " + std::to_string(config.overlap_ms) + " ms)"));
    }
    if (config.overlap_ms >= config.window_duration_ms) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::InvalidArgument,
            "window overlap (" + std::to_string(config.overlap_ms) + " ms) must be smaller than the window duration (" +
            std::to_string(config.window_duration_ms) + " ms)"));
    }
    return core::Result<void>();
}

std::vector<Window> generate_windows(int64_t total_duration_ms, const WindowingConfig& config) {
    std::vector<Window> windows;
    if (total_duration_ms <= 0 || config.window_duration_ms <= 0) return windows;
    // A negative overlap would leave gaps between windows (media silently
    // dropped) and an overlap of at least the window duration would never
    // advance. Both are clamped here; validate_config() reports them.
    const int64_t overlap = std::clamp<int64_t>(config.overlap_ms, 0, config.window_duration_ms - 1);
    const int64_t step = config.window_duration_ms - overlap;
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
