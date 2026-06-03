#pragma once

#include <cstdint>
#include <vector>

namespace video2vec::sync {

struct SyncedFrame {
    int64_t pts_ms = 0;
    int64_t original_pts = 0;
    int stream_index = 0;
    bool is_keyframe = false;
};

struct SyncedSample {
    int64_t pts_ms = 0;
    int64_t original_pts = 0;
    int stream_index = 0;
    int64_t sample_count = 0;
};

struct DriftReport {
    int64_t video_pts_ms = 0;
    int64_t audio_pts_ms = 0;
    int64_t drift_ms = 0;
    bool corrected = false;
};

class Synchronizer {
public:
    explicit Synchronizer(int64_t max_drift_ms = 20);
    void register_stream(int stream_index, int64_t time_base_num, int64_t time_base_den);
    [[nodiscard]] int64_t to_ms(int stream_index, int64_t pts) const;
    [[nodiscard]] int64_t from_ms(int stream_index, int64_t ms) const;
    [[nodiscard]] DriftReport align(int64_t video_pts_ms, int64_t audio_pts_ms);
    [[nodiscard]] int64_t snap(int64_t pts_ms, int64_t reference_pts_ms) const;
    [[nodiscard]] int64_t max_drift_ms() const noexcept { return max_drift_ms_; }
    void set_max_drift_ms(int64_t drift) { max_drift_ms_ = drift; }
private:
    struct StreamTimebase { int64_t num = 1; int64_t den = 1; };
    int64_t max_drift_ms_;
    std::vector<StreamTimebase> timebases_;
};

} // namespace video2vec::sync
