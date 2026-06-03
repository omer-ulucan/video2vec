#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace video2vec::ffmpeg {

class AudioBuffer {
public:
    AudioBuffer() = default;
    explicit AudioBuffer(int sample_rate, int channels);
    void append(std::span<const float> samples);
    void append(std::span<const int16_t> samples);
    [[nodiscard]] std::span<const float> float_data() const;
    [[nodiscard]] std::span<const int16_t> int16_data() const;
    [[nodiscard]] size_t sample_count() const noexcept { return samples_.size() / channels_; }
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
