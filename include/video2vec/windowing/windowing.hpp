#pragma once

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

std::vector<Window> generate_windows(int64_t total_duration_ms, const WindowingConfig& config);

template <typename T>
std::vector<std::vector<T>> assign_to_windows(const std::vector<T>& items,
                                               const std::vector<Window>& windows,
                                               std::function<int64_t(const T&)> get_timestamp) {
    std::vector<std::vector<T>> result(windows.size());
    for (const auto& item : items) {
        int64_t ts = get_timestamp(item);
        for (size_t i = 0; i < windows.size(); ++i) {
            if (windows[i].contains(ts)) result[i].push_back(item);
        }
    }
    return result;
}

} // namespace video2vec::windowing
