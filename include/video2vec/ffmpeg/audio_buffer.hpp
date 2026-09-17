#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace video2vec::ffmpeg {

// Interleaved float PCM buffer. Channel counts below 1 are clamped to 1 so
// sample_count() can never divide by zero.
class AudioBuffer {
public:
    AudioBuffer() = default;
    explicit AudioBuffer(int sample_rate, int channels);
    void append(std::span<const float> samples);
    void append(std::span<const int16_t> samples);
    [[nodiscard]] std::span<const float> float_data() const;
    // Converted copy of the samples as signed 16-bit PCM (input clamped to [-1, 1]).
    [[nodiscard]] std::vector<int16_t> to_int16() const;
    [[nodiscard]] size_t sample_count() const noexcept { return samples_.size() / static_cast<size_t>(channels_); }
    [[nodiscard]] int sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] int channels() const noexcept { return channels_; }
    void clear();
    void reserve(size_t samples);
    [[nodiscard]] size_t byte_size() const noexcept { return samples_.size() * sizeof(float); }
private:
    int sample_rate_ = 16000;
    int channels_ = 1;
    std::vector<float> samples_;
};

} // namespace video2vec::ffmpeg
