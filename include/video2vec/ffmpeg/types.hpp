#pragma once

#include <cstdint>
#include <string>

namespace video2vec::ffmpeg {

enum class MediaType {
    Unknown,
    Video,
    Audio,
    Subtitle,
    Data,
    Attachment
};

struct Rational {
    int num = 1;
    int den = 1;
};

struct StreamInfo {
    int index = -1;
    int64_t duration_pts = 0;
    double duration_seconds = 0.0;
    int width = 0;
    int height = 0;
    int sample_rate = 0;
    int channels = 0;
    MediaType type = MediaType::Unknown;
    std::string codec_name;
    Rational time_base{1, 1};
};

struct VideoProperties {
    int width = 0;
    int height = 0;
    double fps = 0.0;
    int64_t frame_count = 0;
    Rational time_base{1, 1};
};

struct AudioProperties {
    int sample_rate = 0;
    int channels = 0;
    int64_t sample_count = 0;
    Rational time_base{1, 1};
};

} // namespace video2vec::ffmpeg
