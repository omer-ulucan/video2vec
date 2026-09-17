#include <gtest/gtest.h>
#include <video2vec/ffmpeg/demuxer.hpp>
#include "ffmpeg/decoder_impl.hpp"
#include "ffmpeg/ffmpeg_helpers.hpp"
#include <video2vec/asr/whisper_backend.hpp>
#include <video2vec/ocr/tesseract_backend.hpp>
#include <video2vec/embedding/onnx_backend.hpp>
#include <video2vec/core/logger.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

using namespace video2vec;

// End-to-end: demux the generated fixture (tests/data/sample_video.mp4, a
// 640x480 H.264 test pattern with a 440 Hz AAC sine), decode a video frame and
// a few seconds of audio through the codec-parameter path, then run each real
// backend on the decoded data. Every stage must succeed; nothing is optional.
TEST(EndToEnd, SampleVideoPipeline) {
    core::Logger::initialize("e2e_test");
    std::string video_path = TESTS_DIR "/data/sample_video.mp4";

    ffmpeg::Demuxer demuxer;
    int ret = demuxer.open(video_path);
    if (ret < 0) {
        GTEST_SKIP() << "Sample video not available (run scripts/ci-setup-deps.sh): " << video_path;
    }
    ASSERT_TRUE(demuxer.is_open());
    const int video_idx = demuxer.video_stream_index();
    const int audio_idx = demuxer.audio_stream_index();
    ASSERT_GE(video_idx, 0) << "fixture has no video stream";
    ASSERT_GE(audio_idx, 0) << "fixture has no audio stream";
    EXPECT_GT(demuxer.duration_ms(), 4000);

    // Decoders initialized from codec parameters so H.264/AAC extradata is present.
    ffmpeg::Decoder video_decoder;
    ASSERT_EQ(video_decoder.initialize(ffmpeg::native_codec_parameters(demuxer, video_idx)), 0);
    ffmpeg::AudioDecoder audio_decoder;
    ASSERT_EQ(audio_decoder.initialize(ffmpeg::native_codec_parameters(demuxer, audio_idx), 16000), 0);

    ffmpeg::Packet packet;
    ffmpeg::Frame frame;
    std::vector<uint8_t> rgb_frame;
    int width = 0, height = 0;
    std::vector<float> audio;
    const size_t wanted_samples = 16000 * 3;

    for (int guard = 0; guard < 5000; ++guard) {
        if (demuxer.read_packet(packet) < 0) break;
        if (packet.stream_index() == video_idx && rgb_frame.empty()) {
            if (video_decoder.send_packet(packet) == 0) {
                while (video_decoder.receive_frame(frame) == 0) {
                    rgb_frame = ffmpeg::frame_to_rgb(frame, width, height);
                    if (!rgb_frame.empty()) break;
                }
            }
        } else if (packet.stream_index() == audio_idx && audio.size() < wanted_samples) {
            if (audio_decoder.send_packet(packet) == 0) {
                while (audio_decoder.receive_samples(audio) >= 0) {}
            }
        }
        if (!rgb_frame.empty() && audio.size() >= wanted_samples) break;
    }

    ASSERT_FALSE(rgb_frame.empty()) << "no video frame decoded";
    EXPECT_EQ(width, 640);
    EXPECT_EQ(height, 480);
    ASSERT_EQ(rgb_frame.size(), static_cast<size_t>(width) * height * 3);
    ASSERT_GE(audio.size(), static_cast<size_t>(16000)) << "less than one second of audio decoded";
    double energy = 0.0;
    for (float s : audio) energy += static_cast<double>(s) * s;
    EXPECT_GT(energy / static_cast<double>(audio.size()), 1e-4) << "decoded audio is silent";

    // OCR on the decoded frame at its real dimensions.
    {
        ocr::TesseractBackend ocr;
        auto ocr_init = ocr.initialize("eng", "");
        ASSERT_TRUE(ocr_init.ok()) << ocr_init.error().message;
        auto ocr_result = ocr.recognize(rgb_frame, width, height, 3);
        ASSERT_TRUE(ocr_result.ok()) << ocr_result.error().message;
        EXPECT_EQ(ocr_result.value().image_width, width);
        EXPECT_EQ(ocr_result.value().image_height, height);
        core::Logger::info("OCR lines: " + std::to_string(ocr_result.value().lines.size()), {});
    }

    // ASR on the decoded audio.
    {
        asr::WhisperBackend asr;
        std::string model = TESTS_DIR "/models/ggml-tiny.bin";
        auto asr_init = asr.initialize(model, "en");
        ASSERT_TRUE(asr_init.ok()) << asr_init.error().message;
        size_t n = std::min(audio.size(), static_cast<size_t>(16000 * 5));
        auto asr_result = asr.transcribe(std::span<const float>(audio.data(), n), 16000);
        ASSERT_TRUE(asr_result.ok()) << asr_result.error().message;
        core::Logger::info("ASR segments: " + std::to_string(asr_result.value().segments.size()), {});
    }

    // Embedding of a synthetic patch through the tiny image model.
    {
        embedding::ONNXBackend embed;
        std::string model = TESTS_DIR "/models/tiny_image.onnx";
        embedding::EmbeddingConfig cfg{};
        cfg.visual_dim = 512;
        auto emb_init = embed.initialize(model, cfg);
        ASSERT_TRUE(emb_init.ok()) << emb_init.error().message;
        std::vector<std::vector<uint8_t>> patches = {std::vector<uint8_t>(64 * 64 * 3, 128)};
        auto emb_result = embed.encode_images(patches, 64, 64);
        ASSERT_TRUE(emb_result.ok()) << emb_result.error().message;
        ASSERT_EQ(emb_result.value().size(), 1u);
        EXPECT_EQ(emb_result.value()[0].dim, 512);
    }

    core::Logger::info("End-to-end pipeline completed", {});
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
