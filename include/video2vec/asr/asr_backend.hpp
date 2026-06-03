#pragma once

#include "video2vec/core/result.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace video2vec::asr {

struct ASRWord {
    int64_t t_ms = 0;
    int64_t dt_ms = 0;
    std::string text;
    double confidence = 1.0;
};

struct ASRSegment {
    int64_t t0_ms = 0;
    int64_t t1_ms = 0;
    std::string text;
    std::vector<ASRWord> words;
};

struct ASRResult {
    std::vector<ASRSegment> segments;
    std::string full_transcript;
    double language_probability = 1.0;
    std::string detected_language;
};

class IASRBackend {
public:
    virtual ~IASRBackend() = default;
    virtual core::Result<void> initialize(const std::string& model_path, const std::string& language = "auto") = 0;
    virtual core::Result<ASRResult> transcribe(std::span<const float> audio_data, int sample_rate) = 0;
    virtual core::Result<ASRResult> transcribe_stream(std::span<const float> audio_chunk, int sample_rate, bool is_final) = 0;
    virtual void unload() = 0;
    [[nodiscard]] virtual bool is_loaded() const = 0;
    [[nodiscard]] virtual std::string name() const = 0;
};

} // namespace video2vec::asr
