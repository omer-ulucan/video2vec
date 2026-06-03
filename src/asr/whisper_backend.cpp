#include "video2vec/asr/whisper_backend.hpp"
#include "video2vec/core/logger.hpp"
#include <cstring>
#include <vector>

extern "C" {
#include "whisper.h"
}

namespace video2vec::asr {

class WhisperBackend::Impl {
public:
    struct whisper_context* ctx = nullptr;
    std::string model_path;
    std::string language;
    bool loaded = false;
    std::vector<float> stream_buffer;
};

WhisperBackend::WhisperBackend() : impl_(std::make_unique<Impl>()) {}
WhisperBackend::~WhisperBackend() { unload(); }

core::Result<void> WhisperBackend::initialize(const std::string& model_path, const std::string& language) {
    unload();
    impl_->model_path = model_path;
    impl_->language = language;

    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = false;
    impl_->ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);
    if (!impl_->ctx) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::ModelError,
            "Failed to load whisper model: " + model_path));
    }
    impl_->loaded = true;
    core::Logger::info("Whisper model loaded: " + model_path, {});
    return core::Result<void>();
}

core::Result<ASRResult> WhisperBackend::transcribe(std::span<const float> audio_data, int sample_rate) {
    if (!impl_->loaded || !impl_->ctx) {
        return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::ModelError, "Whisper model not loaded"));
    }
    if (sample_rate != 16000) {
        // whisper.cpp expects 16kHz mono float
        return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::InvalidArgument,
            "whisper.cpp requires 16kHz sample rate, got " + std::to_string(sample_rate)));
    }

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.n_threads = 4;
    wparams.language = impl_->language.empty() ? nullptr : impl_->language.c_str();
    wparams.detect_language = impl_->language.empty() || impl_->language == "auto";
    wparams.print_special = false;
    wparams.print_progress = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = false;
    wparams.offset_ms = 0;
    wparams.duration_ms = 0;

    int ret = whisper_full(impl_->ctx, wparams, audio_data.data(), static_cast<int>(audio_data.size()));
    if (ret != 0) {
        return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::ModelError,
            "whisper_full failed with code " + std::to_string(ret)));
    }

    ASRResult result{};
    int n_segments = whisper_full_n_segments(impl_->ctx);
    for (int i = 0; i < n_segments; ++i) {
        const char* seg_text = whisper_full_get_segment_text(impl_->ctx, i);
        if (seg_text) {
            if (!result.full_transcript.empty()) result.full_transcript += " ";
            result.full_transcript += seg_text;
        }
        ASRSegment seg{};
        seg.t0_ms = whisper_full_get_segment_t0(impl_->ctx, i) * 10;
        seg.t1_ms = whisper_full_get_segment_t1(impl_->ctx, i) * 10;
        if (seg_text) seg.text = seg_text;
        result.segments.push_back(std::move(seg));
    }
    result.detected_language = impl_->language;
    result.language_probability = 0.9;
    return core::Result<ASRResult>(std::move(result));
}

core::Result<ASRResult> WhisperBackend::transcribe_stream(std::span<const float> audio_chunk, int sample_rate, bool is_final) {
    impl_->stream_buffer.insert(impl_->stream_buffer.end(), audio_chunk.begin(), audio_chunk.end());
    if (!is_final) {
        ASRResult partial{};
        partial.full_transcript = "[streaming...]";
        return core::Result<ASRResult>(std::move(partial));
    }
    auto result = transcribe(impl_->stream_buffer, sample_rate);
    if (result.ok()) {
        impl_->stream_buffer.clear();
    }
    return result;
}

void WhisperBackend::unload() {
    if (impl_->ctx) {
        whisper_free(impl_->ctx);
        impl_->ctx = nullptr;
    }
    impl_->loaded = false;
    impl_->stream_buffer.clear();
}

bool WhisperBackend::is_loaded() const { return impl_->loaded; }

} // namespace video2vec::asr
