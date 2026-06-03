#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace video2vec::windowing {

struct Window {
    int64_t t0_ms = 0;
    int64_t t1_ms = 0;
    int index = 0;
    [[nodiscard]] int64_t duration_ms() const noexcept { return t1_ms - t0_ms; }
    [[nodiscard]] bool contains(int64_t ts_ms) const noexcept { return ts_ms >= t0_ms && ts_ms < t1_ms; }
    [[nodiscard]] bool overlaps(const Window& other) const noexcept { return t0_ms < other.t1_ms && other.t0_ms < t1_ms; }
};

struct WindowedText {
    int64_t ts_ms = 0;
    int64_t duration_ms = 0;
    std::string text;
    double confidence = 1.0;
};

struct WindowedFrame {
    int64_t ts_ms = 0;
    int64_t pts = 0;
    bool is_keyframe = false;
    int width = 0;
    int height = 0;
};

} // namespace video2vec::windowing
