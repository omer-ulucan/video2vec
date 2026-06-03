#pragma once

#include <cstdint>

namespace video2vec::sync {

inline int64_t pts_to_ms(int64_t pts, int64_t num, int64_t den) {
    if (den == 0) return 0;
    return static_cast<int64_t>(pts * 1000.0 * static_cast<double>(num) / static_cast<double>(den));
}

inline int64_t ms_to_pts(int64_t ms, int64_t num, int64_t den) {
    if (num == 0) return 0;
    return static_cast<int64_t>(ms * static_cast<double>(den) / (1000.0 * static_cast<double>(num)));
}

} // namespace video2vec::sync
