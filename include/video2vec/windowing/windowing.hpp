#pragma once

#include "video2vec/core/result.hpp"
#include "video2vec/windowing/window.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace video2vec::windowing {

class WindowingConfig {
public:
    int64_t window_duration_ms = 45000;
    int64_t overlap_ms = 5000;
};

// Rejects non-positive durations, negative overlaps and overlaps that are
// not smaller than the window. generate_windows() clamps such values
// instead of failing, so callers that take user input should validate first.
core::Result<void> validate_config(const WindowingConfig& config);

// Half-open windows [t0, t1) covering [0, total_duration_ms]; the last window
// is clipped to the total duration.
std::vector<Window> generate_windows(int64_t total_duration_ms, const WindowingConfig& config);

// Assigns each item to every window containing its timestamp. An item whose
// timestamp equals the end of the final (clipped) window belongs to that
// window, so the last instant of the media is never dropped.
template <typename T>
std::vector<std::vector<T>> assign_to_windows(const std::vector<T>& items,
                                               const std::vector<Window>& windows,
                                               std::function<int64_t(const T&)> get_timestamp) {
    std::vector<std::vector<T>> result(windows.size());
    if (windows.empty()) return result;
    for (const auto& item : items) {
        int64_t ts = get_timestamp(item);
        bool assigned = false;
        for (size_t i = 0; i < windows.size(); ++i) {
            if (windows[i].contains(ts)) { result[i].push_back(item); assigned = true; }
        }
        if (!assigned && ts == windows.back().t1_ms) result.back().push_back(item);
    }
    return result;
}

} // namespace video2vec::windowing
