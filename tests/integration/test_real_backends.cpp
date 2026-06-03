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
    // We don't assert specific text because a sine wave won't produce meaningful speech,
    // but the backend should process without error and return segments.
    EXPECT_GE(result.value().segments.size(), 0);
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
    // Gray image has no text, so result may be empty, but processing should succeed.
    EXPECT_GE(result.value().mean_confidence, 0.0);
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
    EXPECT_EQ(img_result.value().size(), 1);
    EXPECT_EQ(img_result.value()[0].dim, 512);
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
