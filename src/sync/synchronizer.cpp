#include "video2vec/sync/synchronizer.hpp"
#include <cmath>
#include <cstdlib>

namespace video2vec::sync {

Synchronizer::Synchronizer(int64_t max_drift_ms) : max_drift_ms_(max_drift_ms) {}

void Synchronizer::register_stream(int stream_index, int64_t time_base_num, int64_t time_base_den) {
    if (static_cast<size_t>(stream_index) >= timebases_.size()) timebases_.resize(stream_index + 1);
    timebases_[stream_index] = {time_base_num, time_base_den};
}

int64_t Synchronizer::to_ms(int stream_index, int64_t pts) const {
    if (static_cast<size_t>(stream_index) >= timebases_.size()) return 0;
    const auto& tb = timebases_[stream_index];
    if (tb.den == 0) return 0;
    return static_cast<int64_t>(pts * 1000.0 * static_cast<double>(tb.num) / static_cast<double>(tb.den));
}

int64_t Synchronizer::from_ms(int stream_index, int64_t ms) const {
    if (static_cast<size_t>(stream_index) >= timebases_.size()) return 0;
    const auto& tb = timebases_[stream_index];
    if (tb.num == 0) return 0;
    return static_cast<int64_t>(ms * static_cast<double>(tb.den) / (1000.0 * static_cast<double>(tb.num)));
}

DriftReport Synchronizer::align(int64_t video_pts_ms, int64_t audio_pts_ms) {
    int64_t drift = std::abs(video_pts_ms - audio_pts_ms);
    DriftReport report{};
    report.video_pts_ms = video_pts_ms;
    report.audio_pts_ms = audio_pts_ms;
    report.drift_ms = drift;
    report.corrected = drift > max_drift_ms_;
    return report;
}

int64_t Synchronizer::snap(int64_t pts_ms, int64_t reference_pts_ms) const {
    int64_t drift = std::abs(pts_ms - reference_pts_ms);
    if (drift > max_drift_ms_) return reference_pts_ms;
    return pts_ms;
}

} // namespace video2vec::sync
