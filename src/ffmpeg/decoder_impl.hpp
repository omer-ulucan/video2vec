#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "video2vec/ffmpeg/media.hpp"

struct AVCodecParameters;

namespace video2vec::ffmpeg {

// Every class in this header is safe to use after being moved from: methods
// on a moved-from instance report failure (negative AVERROR / false / empty)
// instead of dereferencing a null implementation pointer.

class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Decoder(Decoder&&) noexcept;
    Decoder& operator=(Decoder&&) noexcept;

    // Initialize from the codec parameters of a demuxed stream (see
    // native_codec_parameters() in ffmpeg_helpers.hpp). This is the only path
    // that carries codec extradata (H.264 SPS/PPS, AAC AudioSpecificConfig, ...),
    // which container formats such as MP4/MKV require for decoding.
    // Calling initialize() on an initialized decoder releases the old context.
    int initialize(const AVCodecParameters* codec_params);

    // Simplified initialization by codec name for elementary streams whose
    // codecs need no extradata (e.g. "pcm_s16le", "rawvideo").
    int initialize(const char* codec_id, int width, int height, int sample_rate, int channels);

    int send_packet(const Packet& packet);
    // Signals end of stream; remaining buffered frames can then be drained with
    // receive_frame() until it returns AVERROR_EOF.
    int send_eof();
    int receive_frame(Frame& frame);
    // Discards all buffered frames (e.g. after a seek). Use send_eof() to drain instead.
    void flush();
    [[nodiscard]] bool is_initialized() const;
    [[nodiscard]] const char* codec_name() const;

    class Impl;

private:
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

    // Preferred: initialize from demuxed codec parameters (carries extradata).
    int initialize(const AVCodecParameters* codec_params, int target_sample_rate = 16000);
    // Simplified initialization by codec name for raw PCM style streams.
    int initialize(const char* codec_id, int sample_rate, int channels, int target_sample_rate = 16000);

    int send_packet(const Packet& packet);
    int send_eof();
    // Raw decoded frame in the stream's native sample format and rate.
    int receive_frame(Frame& frame);
    // Receives the next decoded frame and appends it to `out` resampled to mono
    // float at target_sample_rate(). Returns the number of samples appended
    // (>= 0), AVERROR(EAGAIN) when more packets are needed, AVERROR_EOF once
    // drained after send_eof(), or another negative AVERROR on failure.
    int receive_samples(std::vector<float>& out);
    // Flushes the resampler's internal delay into `out`. Call once after
    // receive_samples() returned AVERROR_EOF. Returns samples appended.
    int drain_samples(std::vector<float>& out);
    void flush();
    [[nodiscard]] bool is_initialized() const;
    [[nodiscard]] int target_sample_rate() const;

    class Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// Converts a decoded software video frame to packed RGB24 (width*height*3 bytes).
// Returns an empty vector (and zero dimensions) for hardware surfaces, invalid
// frames, or conversion failures.
std::vector<uint8_t> frame_to_rgb(const Frame& frame, int& out_width, int& out_height);

class AudioResampler {
public:
    AudioResampler();
    ~AudioResampler();
    AudioResampler(const AudioResampler&) = delete;
    AudioResampler& operator=(const AudioResampler&) = delete;
    AudioResampler(AudioResampler&&) noexcept;
    AudioResampler& operator=(AudioResampler&&) noexcept;

    // fmt values are raw FFmpeg AVSampleFormat integers (e.g. AV_SAMPLE_FMT_S16 = 1,
    // AV_SAMPLE_FMT_FLT = 3). The output format must be packed (interleaved)
    // when out_channels > 1; planar multi-channel output is rejected.
    bool initialize(int in_sample_rate, int in_channels, int in_fmt,
                    int out_sample_rate, int out_channels, int out_fmt);
    // Returns interleaved bytes in the configured output format. out_samples is
    // the per-channel sample count; the byte size is out_samples * channels *
    // bytes_per_sample(out_fmt).
    std::vector<uint8_t> resample(const Frame& frame, int& out_samples);
    void flush(std::vector<uint8_t>& output, int& out_samples);

    class Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace video2vec::ffmpeg
