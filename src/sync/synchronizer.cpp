#include "video2vec/sync/synchronizer.hpp"
#include "video2vec/sync/timestamp.hpp"
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace video2vec::sync {

namespace {
    // |a - b| without the signed-overflow UB of std::abs(a - b) when the
    // difference does not fit (e.g. AV_NOPTS_VALUE against 0).
    int64_t abs_diff(int64_t a, int64_t b) {
        const uint64_t d = a > b ? static_cast<uint64_t>(a) - static_cast<uint64_t>(b)
                                 : static_cast<uint64_t>(b) - static_cast<uint64_t>(a);
        return d > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
                   ? std::numeric_limits<int64_t>::max()
                   : static_cast<int64_t>(d);
    }
}

Synchronizer::Synchronizer(int64_t max_drift_ms) : max_drift_ms_(max_drift_ms) {}

void Synchronizer::register_stream(int stream_index, int64_t time_base_num, int64_t time_base_den) {
    if (stream_index < 0) throw std::invalid_argument("stream index must not be negative");
    if (time_base_den == 0) throw std::invalid_argument("time base denominator must not be zero");
    if (static_cast<size_t>(stream_index) >= timebases_.size()) timebases_.resize(static_cast<size_t>(stream_index) + 1);
    timebases_[static_cast<size_t>(stream_index)] = {time_base_num, time_base_den};
}

int64_t Synchronizer::to_ms(int stream_index, int64_t pts) const {
    if (stream_index < 0 || static_cast<size_t>(stream_index) >= timebases_.size()) return 0;
    const auto& tb = timebases_[static_cast<size_t>(stream_index)];
    return pts_to_ms(pts, tb.num, tb.den);
}

int64_t Synchronizer::from_ms(int stream_index, int64_t ms) const {
    if (stream_index < 0 || static_cast<size_t>(stream_index) >= timebases_.size()) return 0;
    const auto& tb = timebases_[static_cast<size_t>(stream_index)];
    return ms_to_pts(ms, tb.num, tb.den);
}

DriftReport Synchronizer::align(int64_t video_pts_ms, int64_t audio_pts_ms) {
    int64_t drift = abs_diff(video_pts_ms, audio_pts_ms);
    DriftReport report{};
    report.video_pts_ms = video_pts_ms;
    report.audio_pts_ms = audio_pts_ms;
    report.drift_ms = drift;
    report.corrected = drift > max_drift_ms_;
    return report;
}

int64_t Synchronizer::snap(int64_t pts_ms, int64_t reference_pts_ms) const {
    int64_t drift = abs_diff(pts_ms, reference_pts_ms);
    if (drift > max_drift_ms_) return reference_pts_ms;
    return pts_ms;
}

} // namespace video2vec::sync
