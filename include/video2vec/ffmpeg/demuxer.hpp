#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "video2vec/ffmpeg/types.hpp"
#include "video2vec/ffmpeg/media.hpp"

namespace video2vec::ffmpeg {

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

    // Returns 0 on success, negative AVERROR on failure or EOF.
    int read_packet(Packet& packet);

    int seek(int stream_index, int64_t pts, int flags);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace video2vec::ffmpeg
