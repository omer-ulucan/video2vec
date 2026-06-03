#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace video2vec::ffmpeg {

class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Decoder(Decoder&&) noexcept;
    Decoder& operator=(Decoder&&) noexcept;
    int initialize(const AVCodecParameters* codec_params);
    int send_packet(const AVPacket* packet);
    int receive_frame(AVFrame* frame);
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
    int initialize(const AVCodecParameters* codec_params, int target_sample_rate = 16000);
    int send_packet(const AVPacket* packet);
    int receive_frame(AVFrame* frame);
    void flush();
    [[nodiscard]] bool is_initialized() const;
    [[nodiscard]] int target_sample_rate() const;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

std::vector<uint8_t> frame_to_rgb(const AVFrame* frame, int& out_width, int& out_height);

class AudioResampler {
public:
    AudioResampler();
    ~AudioResampler();
    bool initialize(int in_sample_rate, int in_channels, AVSampleFormat in_fmt,
                      int out_sample_rate, int out_channels, AVSampleFormat out_fmt);
    std::vector<uint8_t> resample(const AVFrame* frame, int& out_samples);
    void flush(std::vector<uint8_t>& output, int& out_samples);
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace video2vec::ffmpeg
