#pragma once

#include "video2vec/asr/asr_backend.hpp"
#include <memory>
#include <string>

namespace video2vec::asr {

class WhisperBackend : public IASRBackend {
public:
    WhisperBackend();
    ~WhisperBackend() override;
    WhisperBackend(const WhisperBackend&) = delete;
    WhisperBackend& operator=(const WhisperBackend&) = delete;
    core::Result<void> initialize(const std::string& model_path, const std::string& language = "auto") override;
    core::Result<ASRResult> transcribe(std::span<const float> audio_data, int sample_rate) override;
    core::Result<ASRResult> transcribe_stream(std::span<const float> audio_chunk, int sample_rate, bool is_final) override;
    void unload() override;
    [[nodiscard]] bool is_loaded() const override;
    [[nodiscard]] std::string name() const override { return "whisper.cpp"; }
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace video2vec::asr
