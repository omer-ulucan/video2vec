#include "video2vec/ffmpeg/audio_buffer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace video2vec::ffmpeg {

AudioBuffer::AudioBuffer(int sample_rate, int channels)
    : sample_rate_(sample_rate > 0 ? sample_rate : 16000), channels_(channels > 0 ? channels : 1) {}

void AudioBuffer::append(std::span<const float> samples) {
    samples_.insert(samples_.end(), samples.begin(), samples.end());
}

void AudioBuffer::append(std::span<const int16_t> samples) {
    size_t old_size = samples_.size();
    samples_.resize(old_size + samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        samples_[old_size + i] = static_cast<float>(samples[i]) / 32768.0f;
    }
}

std::span<const float> AudioBuffer::float_data() const {
    return std::span<const float>(samples_.data(), samples_.size());
}

std::vector<int16_t> AudioBuffer::to_int16() const {
    std::vector<int16_t> out(samples_.size());
    for (size_t i = 0; i < samples_.size(); ++i) {
        float scaled = std::clamp(samples_[i], -1.0f, 1.0f) * 32768.0f;
        long rounded = std::lround(scaled);
        out[i] = static_cast<int16_t>(std::clamp(rounded, -32768L, 32767L));
    }
    return out;
}

void AudioBuffer::clear() { samples_.clear(); samples_.shrink_to_fit(); }
void AudioBuffer::reserve(size_t samples) { samples_.reserve(samples * static_cast<size_t>(channels_)); }

} // namespace video2vec::ffmpeg
