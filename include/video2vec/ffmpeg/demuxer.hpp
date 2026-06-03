#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace video2vec::ffmpeg {

struct StreamInfo {
    int index = -1;
    int64_t duration_pts = 0;
    double duration_seconds = 0.0;
    int width = 0;
    int height = 0;
    int sample_rate = 0;
    int channels = 0;
    AVMediaType type = AVMEDIA_TYPE_UNKNOWN;
    std::string codec_name;
    AVRational time_base = {1, 1};
};

struct VideoProperties {
    int width = 0;
    int height = 0;
    double fps = 0.0;
    int64_t frame_count = 0;
    AVRational time_base = {1, 1};
};

struct AudioProperties {
    int sample_rate = 0;
    int channels = 0;
    int64_t sample_count = 0;
    AVRational time_base = {1, 1};
};

class Demuxer {
public:
    Demuxer();
    ~Demuxer();
    Demuxer(const Demuxer&) = delete;
    Demuxer& operator=(const Demuxer&) = delete;
    Demuxer(Demuxer&&) noexcept;
    Demuxer& operator=(Demuxer&&) noexcept;
    int open(const std::string& path);
    void close();
    [[nodiscard]] bool is_open() const;
    [[nodiscard]] std::string path() const;
    [[nodiscard]] int video_stream_index() const;
    [[nodiscard]] int audio_stream_index() const;
    [[nodiscard]] VideoProperties video_properties() const;
    [[nodiscard]] AudioProperties audio_properties() const;
    [[nodiscard]] std::vector<StreamInfo> streams() const;
    int read_packet(AVPacket* packet);
    int seek(int stream_index, int64_t pts, int flags);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace video2vec::ffmpeg
