#include "video2vec/asr/whisper_backend.hpp"
#include "video2vec/core/logger.hpp"
#include <algorithm>
#include <cstring>
#include <thread>
#include <vector>

#include "whisper.h"

namespace video2vec::asr {

namespace {
    constexpr int kWhisperSampleRate = 16000;
    // transcribe_stream() accumulates audio until is_final; cap it so a caller
    // that never finalizes cannot grow the buffer without bound (30 minutes).
    constexpr size_t kMaxStreamSamples = static_cast<size_t>(kWhisperSampleRate) * 60 * 30;

    int default_thread_count() {
        unsigned hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = 4;
        return static_cast<int>(std::min(8u, hw));
    }

    bool is_auto_language(const std::string& language) {
        return language.empty() || language == "auto";
    }

    // Groups whisper's sub-word tokens into words. A token whose text starts
    // with a space begins a new word; special tokens (>= EOT) are skipped.
    void collect_words(whisper_context* ctx, int segment, std::vector<ASRWord>& words) {
        const whisper_token eot = whisper_token_eot(ctx);
        const int n_tokens = whisper_full_n_tokens(ctx, segment);
        ASRWord current{};
        int64_t current_t1 = 0;
        double prob_sum = 0.0;
        int prob_count = 0;

        auto flush = [&]() {
            if (current.text.empty()) return;
            current.dt_ms = std::max<int64_t>(0, current_t1 - current.t_ms);
            current.confidence = prob_count > 0 ? prob_sum / prob_count : 0.0;
            words.push_back(std::move(current));
            current = ASRWord{};
            prob_sum = 0.0;
            prob_count = 0;
        };

        for (int j = 0; j < n_tokens; ++j) {
            const whisper_token_data td = whisper_full_get_token_data(ctx, segment, j);
            if (td.id >= eot) continue;
            const char* text = whisper_full_get_token_text(ctx, segment, j);
            if (!text || !*text) continue;
            const bool starts_word = text[0] == ' ';
            if (starts_word) flush();
            if (current.text.empty()) current.t_ms = td.t0 * 10;
            current.text += (starts_word ? text + 1 : text);
            current_t1 = td.t1 * 10;
            prob_sum += td.p;
            ++prob_count;
        }
        flush();
    }
}

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
    if (!is_auto_language(language) && whisper_lang_id(language.c_str()) < 0) {
        unload();
        return core::Result<void>(core::Error::from_code(core::ErrorCode::InvalidArgument,
            "Unknown whisper language code: " + language));
    }
    impl_->loaded = true;
    core::Logger::info("Whisper model loaded: " + model_path, {});
    return core::Result<void>();
}

core::Result<ASRResult> WhisperBackend::transcribe(std::span<const float> audio_data, int sample_rate) {
    if (!impl_->loaded || !impl_->ctx) {
        return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::ModelError, "Whisper model not loaded"));
    }
    if (sample_rate != kWhisperSampleRate) {
        // whisper.cpp expects 16kHz mono float
        return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::InvalidArgument,
            "whisper.cpp requires 16kHz sample rate, got " + std::to_string(sample_rate)));
    }
    if (audio_data.empty()) {
        return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::InvalidArgument, "audio buffer is empty"));
    }

    const int n_threads = default_thread_count();
    const int n_samples = static_cast<int>(std::min<size_t>(audio_data.size(), static_cast<size_t>(INT32_MAX)));

    ASRResult result{};
    std::string language = impl_->language;
    result.language_probability = 1.0;

    if (is_auto_language(language)) {
        // Detect the language explicitly so the probability is available and
        // whisper_full can be given a concrete language. Note that setting
        // params.detect_language instead makes whisper_full return right after
        // detection, without transcribing anything.
        language = "auto";
        result.language_probability = 0.0;
        if (whisper_pcm_to_mel(impl_->ctx, audio_data.data(), n_samples, n_threads) == 0) {
            std::vector<float> probs(static_cast<size_t>(whisper_lang_max_id()) + 1, 0.0f);
            int lang_id = whisper_lang_auto_detect(impl_->ctx, 0, n_threads, probs.data());
            if (lang_id >= 0) {
                language = whisper_lang_str(lang_id);
                result.language_probability = probs[static_cast<size_t>(lang_id)];
            }
        }
    }

    whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    wparams.n_threads = n_threads;
    wparams.language = language.c_str();
    wparams.detect_language = false;
    wparams.token_timestamps = true;
    wparams.print_special = false;
    wparams.print_progress = false;
    wparams.print_realtime = false;
    wparams.print_timestamps = false;
    wparams.offset_ms = 0;
    wparams.duration_ms = 0;

    int ret = whisper_full(impl_->ctx, wparams, audio_data.data(), n_samples);
    if (ret != 0) {
        return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::ModelError,
            "whisper_full failed with code " + std::to_string(ret)));
    }

    int n_segments = whisper_full_n_segments(impl_->ctx);
    for (int i = 0; i < n_segments; ++i) {
        const char* seg_text = whisper_full_get_segment_text(impl_->ctx, i);
        ASRSegment seg{};
        seg.t0_ms = whisper_full_get_segment_t0(impl_->ctx, i) * 10;  // whisper reports centiseconds
        seg.t1_ms = whisper_full_get_segment_t1(impl_->ctx, i) * 10;
        if (seg_text) {
            seg.text = seg_text;
            if (!result.full_transcript.empty()) result.full_transcript += " ";
            result.full_transcript += seg_text;
        }
        collect_words(impl_->ctx, i, seg.words);
        result.segments.push_back(std::move(seg));
    }

    const int full_lang = whisper_full_lang_id(impl_->ctx);
    const char* full_lang_str = full_lang >= 0 ? whisper_lang_str(full_lang) : nullptr;
    result.detected_language = full_lang_str ? full_lang_str : language;
    return core::Result<ASRResult>(std::move(result));
}

core::Result<ASRResult> WhisperBackend::transcribe_stream(std::span<const float> audio_chunk, int sample_rate, bool is_final) {
    if (!impl_->loaded || !impl_->ctx) {
        return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::ModelError, "Whisper model not loaded"));
    }
    if (sample_rate != kWhisperSampleRate) {
        return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::InvalidArgument,
            "whisper.cpp requires 16kHz sample rate, got " + std::to_string(sample_rate)));
    }
    if (impl_->stream_buffer.size() + audio_chunk.size() > kMaxStreamSamples) {
        impl_->stream_buffer.clear();
        return core::Result<ASRResult>(core::Error::from_code(core::ErrorCode::InvalidArgument,
            "streaming buffer exceeded " + std::to_string(kMaxStreamSamples / kWhisperSampleRate) + " seconds without a final chunk"));
    }
    impl_->stream_buffer.insert(impl_->stream_buffer.end(), audio_chunk.begin(), audio_chunk.end());
    if (!is_final) {
        ASRResult partial{};
        partial.full_transcript = "[streaming...]";
        return core::Result<ASRResult>(std::move(partial));
    }
    auto result = transcribe(impl_->stream_buffer, sample_rate);
    impl_->stream_buffer.clear();  // a failed final chunk must not be retried against a growing buffer
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
