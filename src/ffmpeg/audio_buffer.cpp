#include "video2vec/ffmpeg/audio_buffer.hpp"
#include <algorithm>
#include <cstring>

namespace video2vec::ffmpeg {

AudioBuffer::AudioBuffer(int sample_rate, int channels)
    : sample_rate_(sample_rate), channels_(channels) {}

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

std::span<const int16_t> AudioBuffer::int16_data() const { return {}; }

void AudioBuffer::clear() { samples_.clear(); samples_.shrink_to_fit(); }
void AudioBuffer::reserve(size_t samples) { samples_.reserve(samples * channels_); }

} // namespace video2vec::ffmpeg
