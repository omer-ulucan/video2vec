#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "video2vec/ffmpeg/media.hpp"

struct AVCodecParameters;

namespace video2vec::ffmpeg {

class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Decoder(Decoder&&) noexcept;
    Decoder& operator=(Decoder&&) noexcept;

    // Initialize from codec parameters extracted from demuxer streams.
    int initialize(const AVCodecParameters* codec_params);

    // Simplified initialization for cases where raw parameters are unavailable.
    // codec_id: e.g. "h264", "aac", etc.
    int initialize(const char* codec_id, int width, int height, int sample_rate, int channels);

    int send_packet(const Packet& packet);
    int receive_frame(Frame& frame);
    void flush();
    [[nodiscard]] bool is_initialized() const;
    [[nodiscard]] const char* codec_name() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();
    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;
    AudioDecoder(AudioDecoder&&) noexcept;
    AudioDecoder& operator=(AudioDecoder&&) noexcept;

    int initialize(const char* codec_id, int sample_rate, int channels, int target_sample_rate = 16000);
    int send_packet(const Packet& packet);
    int receive_frame(Frame& frame);
    void flush();
    [[nodiscard]] bool is_initialized() const;
    [[nodiscard]] int target_sample_rate() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

std::vector<uint8_t> frame_to_rgb(const Frame& frame, int& out_width, int& out_height);

class AudioResampler {
public:
    AudioResampler();
    ~AudioResampler();
    AudioResampler(const AudioResampler&) = delete;
    AudioResampler& operator=(const AudioResampler&) = delete;
    AudioResampler(AudioResampler&&) noexcept;
    AudioResampler& operator=(AudioResampler&&) noexcept;

    // fmt values are opaque raw FFmpeg sample format integers (e.g. AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLT).
    bool initialize(int in_sample_rate, int in_channels, int in_fmt,
                    int out_sample_rate, int out_channels, int out_fmt);
    std::vector<uint8_t> resample(const Frame& frame, int& out_samples);
    void flush(std::vector<uint8_t>& output, int& out_samples);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace video2vec::ffmpeg
