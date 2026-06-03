#include "video2vec/asr/whisper_backend.hpp"
#include "video2vec/core/logger.hpp"
#include <whisper.h>
#include <cstring>

namespace video2vec::asr {

class WhisperBackend::Impl {
public:
    whisper_context* ctx_ = nullptr;
    whisper_full_params params_ = {};
    std::string model_path_;
    std::string language_;
    bool loaded_ = false;
    std::vector<float> stream_buffer_;
};

WhisperBackend::WhisperBackend() : impl_(std::make_unique<Impl>()) {}
WhisperBackend::~WhisperBackend() { unload(); }

core::Result<void> WhisperBackend::initialize(const std::string& model_path, const std::string& language) {
    unload();
    impl_->model_path_ = model_path;
    impl_->language_ = language;
    whisper_context_params cparams = whisper_context_default_params();
    impl_->ctx_ = whisper_init_from_file_with_params(model_path.c_str(), cparams);
    if (!impl_->ctx_) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::ModelError,
            "failed to load whisper model: " + model_path));
    }
    impl_->params_ = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    impl_->params_.print_realtime = false;
    impl_->params_.print_progress = false;
    impl_->params_.print_timestamps = false;
    impl_->params_.print_special = false;
    impl_->params_.translate = false;
    impl_->params_.language = language == "auto" ? nullptr : language.c_str();
    impl_->params_.n_threads = 4;
    impl_->params_.max_len = 0;
    impl_->params_.token_timestamps = true;
    impl_->loaded_ = true;
    return core::Result<void>();
}

core::Result<ASRResult> WhisperBackend::transcribe(std::span<const float> audio_data, int sample_rate) {
    if (!impl_->loaded_ || !impl_->ctx_) {
        return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::ModelError, "whisper model not loaded"));
    }
    if (sample_rate != 16000) {
        return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::InvalidArgument, "whisper requires 16kHz audio"));
    }
    int ret = whisper_full(impl_->ctx_, impl_->params_, audio_data.data(), static_cast<int>(audio_data.size()));
    if (ret != 0) {
        return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::ModelError,
            "whisper transcription failed: " + std::to_string(ret)));
    }
    ASRResult result{};
    int n_segments = whisper_full_n_segments(impl_->ctx_);
    for (int i = 0; i < n_segments; ++i) {
        ASRSegment seg{};
        seg.t0_ms = whisper_full_get_segment_t0(impl_->ctx_, i) * 10;
        seg.t1_ms = whisper_full_get_segment_t1(impl_->ctx_, i) * 10;
        seg.text = whisper_full_get_segment_text(impl_->ctx_, i);
        result.full_transcript += seg.text + " ";
        int n_tokens = whisper_full_n_tokens(impl_->ctx_, i);
        for (int j = 0; j < n_tokens; ++j) {
            ASRWord word{};
            word.t_ms = seg.t0_ms + whisper_full_get_token_t(impl_->ctx_, i, j) * 10;
            word.text = whisper_full_get_token_text(impl_->ctx_, i, j);
            word.confidence = 1.0;
            seg.words.push_back(std::move(word));
        }
        result.segments.push_back(std::move(seg));
    }
    return core::Result<ASRResult>(std::move(result));
}

core::Result<ASRResult> WhisperBackend::transcribe_stream(std::span<const float> audio_chunk, int sample_rate, bool is_final) {
    impl_->stream_buffer_.insert(impl_->stream_buffer_.end(), audio_chunk.begin(), audio_chunk.end());
    if (is_final) {
        auto result = transcribe(impl_->stream_buffer_, sample_rate);
        impl_->stream_buffer_.clear();
        return result;
    }
    return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::Unsupported, "streaming intermediate results not yet implemented"));
}

void WhisperBackend::unload() {
    if (impl_->ctx_) { whisper_free(impl_->ctx_); impl_->ctx_ = nullptr; }
    impl_->loaded_ = false;
    impl_->stream_buffer_.clear();
}

bool WhisperBackend::is_loaded() const { return impl_->loaded_; }

} // namespace video2vec::asr
