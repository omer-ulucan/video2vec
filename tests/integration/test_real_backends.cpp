#include <gtest/gtest.h>
#include <video2vec/asr/whisper_backend.hpp>
#include <video2vec/ocr/tesseract_backend.hpp>
#include <video2vec/embedding/onnx_backend.hpp>
#include <video2vec/ffmpeg/demuxer.hpp>
#include <video2vec/core/logger.hpp>
#include <cmath>
#include <fstream>
#include <vector>

using namespace video2vec;

// ------------------------------------------------------------------
// Helper: Generate synthetic 16kHz mono float audio (sine wave)
// ------------------------------------------------------------------
static std::vector<float> generate_sine_audio(float freq, float duration_sec, int sample_rate = 16000) {
    int samples = static_cast<int>(duration_sec * sample_rate);
    std::vector<float> data(samples);
    for (int i = 0; i < samples; ++i) {
        data[i] = std::sin(2.0f * 3.14159265f * freq * static_cast<float>(i) / static_cast<float>(sample_rate));
    }
    return data;
}

// ------------------------------------------------------------------
// Helper: Generate synthetic RGB image data with a gray background
// ------------------------------------------------------------------
static std::vector<uint8_t> generate_gray_image(int width, int height) {
    std::vector<uint8_t> data(width * height * 3, 200);
    return data;
}

// ------------------------------------------------------------------
// Test: Actual whisper.cpp inference
// ------------------------------------------------------------------
TEST(RealBackends, WhisperInference) {
    core::Logger::initialize("test_whisper");
    asr::WhisperBackend backend;
    std::string model_path = TESTS_DIR "/models/ggml-tiny.bin";
    auto init = backend.initialize(model_path, "en");
    if (!init) {
        GTEST_SKIP() << "Whisper model not available: " << init.error().message;
    }
    ASSERT_TRUE(backend.is_loaded());

    auto audio = generate_sine_audio(440.0f, 2.0f, 16000);
    auto result = backend.transcribe(audio, 16000);
    ASSERT_TRUE(result.ok()) << result.error().message;
    // A sine wave carries no speech, so no text is asserted, but the backend
    // must run to completion with the explicit language echoed back.
    EXPECT_EQ(result.value().detected_language, "en");
    EXPECT_DOUBLE_EQ(result.value().language_probability, 1.0);
    for (const auto& seg : result.value().segments) {
        EXPECT_LE(seg.t0_ms, seg.t1_ms);
        for (const auto& w : seg.words) {
            EXPECT_FALSE(w.text.empty());
            EXPECT_GE(w.t_ms, seg.t0_ms);
            EXPECT_GE(w.confidence, 0.0);
            EXPECT_LE(w.confidence, 1.0);
        }
    }

    EXPECT_FALSE(backend.transcribe(audio, 44100).ok());
    EXPECT_FALSE(backend.transcribe(std::span<const float>(), 16000).ok());
}

// The CLI initializes whisper with language "auto". With detect_language
// enabled whisper_full returned before decoding anything, so this path used
// to yield zero segments and "auto" as the detected language.
TEST(RealBackends, WhisperAutoLanguageStillTranscribes) {
    core::Logger::initialize("test_whisper_auto");
    asr::WhisperBackend backend;
    std::string model_path = TESTS_DIR "/models/ggml-tiny.bin";
    auto init = backend.initialize(model_path, "auto");
    if (!init) {
        GTEST_SKIP() << "Whisper model not available: " << init.error().message;
    }
    auto audio = generate_sine_audio(440.0f, 3.0f, 16000);
    auto result = backend.transcribe(audio, 16000);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_NE(result.value().detected_language, "auto");
    EXPECT_FALSE(result.value().detected_language.empty());
    EXPECT_GE(result.value().language_probability, 0.0);
    EXPECT_LE(result.value().language_probability, 1.0);

    // Streaming: partial chunks return a placeholder, the final chunk transcribes.
    auto partial = backend.transcribe_stream(std::span<const float>(audio.data(), 16000), 16000, false);
    ASSERT_TRUE(partial.ok());
    EXPECT_EQ(partial.value().full_transcript, "[streaming...]");
    auto final_result = backend.transcribe_stream(std::span<const float>(audio.data() + 16000, audio.size() - 16000), 16000, true);
    ASSERT_TRUE(final_result.ok()) << final_result.error().message;
    EXPECT_NE(final_result.value().detected_language, "auto");

    asr::WhisperBackend unloaded;
    EXPECT_FALSE(unloaded.transcribe_stream(audio, 16000, false).ok());
    EXPECT_FALSE(backend.initialize(model_path, "not-a-language").ok());
}

// ------------------------------------------------------------------
// Test: Actual Tesseract OCR
// ------------------------------------------------------------------
TEST(RealBackends, TesseractOCR) {
    core::Logger::initialize("test_ocr");
    ocr::TesseractBackend backend;
    auto init = backend.initialize("eng", "");
    if (!init) {
        GTEST_SKIP() << "Tesseract not available: " << init.error().message;
    }
    ASSERT_TRUE(backend.is_loaded());

    auto image = generate_gray_image(200, 50);
    auto result = backend.recognize(image, 200, 50, 3);
    ASSERT_TRUE(result.ok()) << result.error().message;
    // A flat gray image has no text: no lines, and therefore no confidence.
    EXPECT_EQ(result.value().image_width, 200);
    EXPECT_EQ(result.value().image_height, 50);
    EXPECT_TRUE(result.value().lines.empty());
    EXPECT_DOUBLE_EQ(result.value().mean_confidence, 0.0);

    // Single-channel and RGBA inputs are accepted too.
    std::vector<uint8_t> gray1(200 * 50, 200);
    EXPECT_TRUE(backend.recognize(gray1, 200, 50, 1).ok());
    std::vector<uint8_t> rgba(200 * 50 * 4, 200);
    EXPECT_TRUE(backend.recognize(rgba, 200, 50, 4).ok());

    // Buffers that are too short used to be zero-filled and OCR'd as black.
    std::vector<uint8_t> short_buf(100, 200);
    auto short_result = backend.recognize(short_buf, 200, 50, 3);
    ASSERT_FALSE(short_result.ok());
    EXPECT_EQ(short_result.error().code, core::make_error_code(core::ErrorCode::InvalidArgument));
    EXPECT_FALSE(backend.recognize(image, 200, 50, 2).ok());
    EXPECT_FALSE(backend.recognize(image, 0, 50, 3).ok());

    backend.unload();
    EXPECT_FALSE(backend.is_loaded());
    EXPECT_FALSE(backend.recognize(image, 200, 50, 3).ok());
}

// Renders a crude block-letter "HI" into an RGB image so the iterator path
// (per-line text, bbox, confidence) can be checked without font rendering.
static std::vector<uint8_t> render_hi(int width, int height) {
    std::vector<uint8_t> img(static_cast<size_t>(width) * height * 3, 255);
    auto fill = [&](int x0, int y0, int w, int h) {
        for (int y = y0; y < y0 + h; ++y)
            for (int x = x0; x < x0 + w; ++x) {
                size_t i = (static_cast<size_t>(y) * width + x) * 3;
                img[i] = img[i + 1] = img[i + 2] = 0;
            }
    };
    // H: two verticals and a bar; I: one vertical with serifs. 60px tall glyphs.
    fill(40, 30, 12, 60); fill(88, 30, 12, 60); fill(40, 54, 60, 12);
    fill(130, 30, 40, 10); fill(144, 30, 12, 60); fill(130, 80, 40, 10);
    return img;
}

TEST(RealBackends, TesseractLinesCarryBoundingBoxes) {
    core::Logger::initialize("test_ocr_lines");
    ocr::TesseractBackend backend;
    auto init = backend.initialize("eng", "");
    if (!init) {
        GTEST_SKIP() << "Tesseract not available: " << init.error().message;
    }
    const int w = 220, h = 120;
    auto image = render_hi(w, h);
    auto result = backend.recognize(image, w, h, 3);
    ASSERT_TRUE(result.ok()) << result.error().message;
    ASSERT_FALSE(result.value().lines.empty()) << "no text line recognized";
    for (const auto& line : result.value().lines) {
        EXPECT_FALSE(line.text.empty());
        EXPECT_GE(line.bbox.x, 0);
        EXPECT_GE(line.bbox.y, 0);
        EXPECT_GT(line.bbox.w, 0);
        EXPECT_GT(line.bbox.h, 0);
        EXPECT_LE(line.bbox.x + line.bbox.w, w);
        EXPECT_LE(line.bbox.y + line.bbox.h, h);
        EXPECT_GE(line.confidence, 0.0);
        EXPECT_LE(line.confidence, 1.0);
        // The line box must be tighter than the whole image (the old code returned the full frame).
        EXPECT_LT(line.bbox.w * line.bbox.h, w * h);
    }
    EXPECT_GT(result.value().mean_confidence, 0.0);
}

// ------------------------------------------------------------------
// Test: Actual ONNX Runtime embedding generation
// ------------------------------------------------------------------
TEST(RealBackends, ONNXEmbedding) {
    core::Logger::initialize("test_onnx");
    // Test text encoding
    embedding::ONNXBackend text_backend;
    std::string text_model = TESTS_DIR "/models/tiny_embedding.onnx";
    embedding::EmbeddingConfig text_cfg{};
    text_cfg.visual_dim = 512;
    text_cfg.text_dim = 512;
    auto text_init = text_backend.initialize(text_model, text_cfg);
    if (!text_init) {
        GTEST_SKIP() << "ONNX text model not available: " << text_init.error().message;
    }
    ASSERT_TRUE(text_backend.is_loaded());
    auto text_result = text_backend.encode_text({"hello world", "test phrase"});
    ASSERT_TRUE(text_result.ok()) << text_result.error().message;
    EXPECT_EQ(text_result.value().size(), 2);
    EXPECT_EQ(text_result.value()[0].dim, 512);

    // Test image encoding
    embedding::ONNXBackend img_backend;
    std::string img_model = TESTS_DIR "/models/tiny_image.onnx";
    embedding::EmbeddingConfig img_cfg{};
    img_cfg.visual_dim = 512;
    img_cfg.text_dim = 384;
    auto img_init = img_backend.initialize(img_model, img_cfg);
    if (!img_init) {
        GTEST_SKIP() << "ONNX image model not available: " << img_init.error().message;
    }
    ASSERT_TRUE(img_backend.is_loaded());
    std::vector<std::vector<uint8_t>> images = {generate_gray_image(64, 64)};
    auto img_result = img_backend.encode_images(images, 64, 64);
    ASSERT_TRUE(img_result.ok()) << img_result.error().message;
    ASSERT_EQ(img_result.value().size(), 1u);
    EXPECT_EQ(img_result.value()[0].dim, 512);
    EXPECT_EQ(img_result.value()[0].int8_data.size(), 512u);
    EXPECT_GT(img_result.value()[0].int8_scale, 0.0f);

    // A wrong-sized buffer or a shape the model does not accept is an error,
    // not a silently zero-padded tensor.
    std::vector<std::vector<uint8_t>> short_image = {std::vector<uint8_t>(10, 0)};
    EXPECT_FALSE(img_backend.encode_images(short_image, 64, 64).ok());
    std::vector<std::vector<uint8_t>> other_size = {generate_gray_image(32, 32)};
    EXPECT_FALSE(img_backend.encode_images(other_size, 32, 32).ok());  // model input is fixed at 64x64
    EXPECT_FALSE(img_backend.encode_images(images, 0, 64).ok());
}

// initialize -> unload -> initialize used to leave stale name tables behind,
// so every Run() after a re-initialize failed.
TEST(RealBackends, ONNXReinitializeAndFloatStorage) {
    core::Logger::initialize("test_onnx_reinit");
    embedding::ONNXBackend backend;
    std::string img_model = TESTS_DIR "/models/tiny_image.onnx";
    embedding::EmbeddingConfig cfg{};
    cfg.visual_dim = 512;
    cfg.storage_quant = embedding::Quantization::FP32;
    auto init = backend.initialize(img_model, cfg);
    if (!init) {
        GTEST_SKIP() << "ONNX image model not available: " << init.error().message;
    }
    backend.unload();
    EXPECT_FALSE(backend.is_loaded());
    EXPECT_FALSE(backend.encode_images(std::vector<std::vector<uint8_t>>{generate_gray_image(64, 64)}, 64, 64).ok());
    ASSERT_TRUE(backend.initialize(img_model, cfg).ok());
    ASSERT_TRUE(backend.initialize(img_model, cfg).ok());  // re-initialize without unload
    std::vector<std::vector<uint8_t>> images = {generate_gray_image(64, 64), generate_gray_image(64, 64)};
    auto result = backend.encode_images(images, 64, 64);
    ASSERT_TRUE(result.ok()) << result.error().message;
    ASSERT_EQ(result.value().size(), 2u);
    EXPECT_EQ(result.value()[0].float_data.size(), 512u);
    EXPECT_TRUE(result.value()[0].int8_data.empty());
    EXPECT_EQ(result.value()[0].float_data, result.value()[1].float_data);
    EXPECT_EQ(embedding::to_float(result.value()[0]), result.value()[0].float_data);

    // A text encoder that wants token ids cannot be driven without a tokenizer.
    EXPECT_FALSE(backend.initialize("/nonexistent/model.onnx", cfg).ok());
    EXPECT_FALSE(backend.is_loaded());
}

TEST(RealBackends, Int8QuantizationRoundTripKeepsScale) {
    std::vector<float> v = {0.5f, -0.25f, 0.125f, 0.0f, -1.5f, 3.0f};
    float scale = 0.0f;
    auto q = embedding::quantize_to_int8(v, scale);
    ASSERT_EQ(q.size(), v.size());
    EXPECT_FLOAT_EQ(scale, 3.0f);
    EXPECT_EQ(q[5], 127);
    auto back = embedding::dequantize_from_int8(q, scale);
    for (size_t i = 0; i < v.size(); ++i) EXPECT_NEAR(back[i], v[i], 3.0f / 127.0f);
    embedding::Embedding emb{};
    emb.int8_data = q;
    emb.int8_scale = scale;
    auto restored = embedding::to_float(emb);
    ASSERT_EQ(restored.size(), v.size());
    EXPECT_NEAR(restored[4], -1.5f, 3.0f / 127.0f);
    float zero_scale = 1.0f;
    EXPECT_TRUE(embedding::quantize_to_int8(std::span<const float>(), zero_scale).empty());
}

// ------------------------------------------------------------------
// Test: End-to-end demux of real video file
// ------------------------------------------------------------------
TEST(RealBackends, VideoDemux) {
    core::Logger::initialize("test_demux");
    ffmpeg::Demuxer demuxer;
    std::string video_path = TESTS_DIR "/data/sample_video.mp4";
    int ret = demuxer.open(video_path);
    if (ret < 0) {
        GTEST_SKIP() << "Sample video not available";
    }
    EXPECT_TRUE(demuxer.is_open());
    auto streams = demuxer.streams();
    EXPECT_GE(streams.size(), 1);
    auto vprops = demuxer.video_properties();
    EXPECT_GT(vprops.width, 0);
    EXPECT_GT(vprops.height, 0);
    auto aprops = demuxer.audio_properties();
    EXPECT_GE(aprops.sample_rate, 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
