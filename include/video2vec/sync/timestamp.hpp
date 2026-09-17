#pragma once

#include <cmath>
#include <cstdint>

namespace video2vec::sync {

// pts * 1000 * num / den rounded to the nearest millisecond. Truncation
// introduced a systematic negative bias (1/30 s became 33 ms, never 34)
// that accumulated over a stream. Returns 0 for a zero denominator.
inline int64_t pts_to_ms(int64_t pts, int64_t num, int64_t den) {
    if (den == 0) return 0;
    const long double ms = static_cast<long double>(pts) * 1000.0L * static_cast<long double>(num) / static_cast<long double>(den);
    return static_cast<int64_t>(std::llround(ms));
}

inline int64_t ms_to_pts(int64_t ms, int64_t num, int64_t den) {
    if (num == 0) return 0;
    const long double pts = static_cast<long double>(ms) * static_cast<long double>(den) / (1000.0L * static_cast<long double>(num));
    return static_cast<int64_t>(std::llround(pts));
}

} // namespace video2vec::sync
