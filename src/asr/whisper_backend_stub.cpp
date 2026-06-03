#include "video2vec/asr/whisper_backend.hpp"
#include "video2vec/core/logger.hpp"

namespace video2vec::asr {

class WhisperBackend::Impl {
public:
    std::string model_path_;
    std::string language_;
    bool loaded_ = false;
    std::vector<float> stream_buffer_;
};

WhisperBackend::WhisperBackend() : impl_(std::make_unique<Impl>()) {}
WhisperBackend::~WhisperBackend() { unload(); }

core::Result<void> WhisperBackend::initialize(const std::string& model_path, const std::string& language) {
    (void)model_path; (void)language;
    return core::Result<void>(core::Error::from_code(core::ErrorCode::Unsupported,
        "whisper.cpp backend not available (compiled without whisper.cpp)"));
}

core::Result<ASRResult> WhisperBackend::transcribe(std::span<const float> audio_data, int sample_rate) {
    (void)audio_data; (void)sample_rate;
    return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::Unsupported,
        "whisper.cpp backend not available (compiled without whisper.cpp)"));
}

core::Result<ASRResult> WhisperBackend::transcribe_stream(std::span<const float> audio_chunk, int sample_rate, bool is_final) {
    (void)audio_chunk; (void)sample_rate; (void)is_final;
    return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::Unsupported,
        "whisper.cpp backend not available (compiled without whisper.cpp)"));
}

void WhisperBackend::unload() {
    impl_->loaded_ = false;
    impl_->stream_buffer_.clear();
}

bool WhisperBackend::is_loaded() const { return impl_->loaded_; }

} // namespace video2vec::asr
