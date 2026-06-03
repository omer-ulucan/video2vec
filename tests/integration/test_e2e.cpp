#include <gtest/gtest.h>
#include <video2vec/ffmpeg/demuxer.hpp>
#include "ffmpeg/decoder_impl.hpp"
#include <video2vec/ffmpeg/audio_buffer.hpp>
#include <video2vec/asr/whisper_backend.hpp>
#include <video2vec/ocr/tesseract_backend.hpp>
#include <video2vec/embedding/onnx_backend.hpp>
#include <video2vec/core/logger.hpp>
#include <fstream>

using namespace video2vec;

TEST(EndToEnd, SampleVideoPipeline) {
    core::Logger::initialize("e2e_test");
    std::string video_path = TESTS_DIR "/data/sample_video.mp4";

    ffmpeg::Demuxer demuxer;
    int ret = demuxer.open(video_path);
    if (ret < 0) {
        GTEST_SKIP() << "Sample video not available";
    }
    ASSERT_TRUE(demuxer.is_open());

    // Decode first video frame
    ffmpeg::Packet packet;
    ffmpeg::Frame frame;
    ffmpeg::Decoder video_decoder;
    bool got_frame = false;
    std::vector<uint8_t> rgb_frame;

    auto streams = demuxer.streams();
    int video_idx = demuxer.video_stream_index();
    int audio_idx = demuxer.audio_stream_index();

    if (video_idx >= 0) {
        auto si = streams[video_idx];
        video_decoder.initialize(si.codec_name.c_str(), si.width, si.height, 0, 0);
        for (int i = 0; i < 100 && !got_frame; ++i) {
            if (demuxer.read_packet(packet) < 0) break;
            if (packet.stream_index() == video_idx) {
                video_decoder.send_packet(packet);
                if (video_decoder.receive_frame(frame) == 0) {
                    int w = 0, h = 0;
                    rgb_frame = ffmpeg::frame_to_rgb(frame, w, h);
                    got_frame = (w > 0 && h > 0);
                }
            }
        }
    }

    // Decode some audio for ASR
    ffmpeg::AudioDecoder audio_decoder;
    ffmpeg::AudioBuffer audio_buffer(16000, 1);
    if (audio_idx >= 0) {
        auto si = streams[audio_idx];
        audio_decoder.initialize(si.codec_name.c_str(), si.sample_rate, si.channels);
        ffmpeg::AudioResampler resampler;
        resampler.initialize(si.sample_rate, si.channels, si.sample_rate == 44100 ? 8 : 1,
                             16000, 1, 1); // 8=AV_SAMPLE_FMT_FLTP, 1=AV_SAMPLE_FMT_FLT
        // Seek back to start for audio
        demuxer.seek(audio_idx, 0, 0);
        ffmpeg::Packet apkt;
        ffmpeg::Frame aframe;
        for (int i = 0; i < 200 && audio_buffer.sample_count() < 16000 * 2; ++i) {
            if (demuxer.read_packet(apkt) < 0) break;
            if (apkt.stream_index() == audio_idx) {
                audio_decoder.send_packet(apkt);
                if (audio_decoder.receive_frame(aframe) == 0) {
                    int out_samples = 0;
                    auto resampled = resampler.resample(aframe, out_samples);
                    if (!resampled.empty()) {
                        audio_buffer.append(std::span<const float>(
                            reinterpret_cast<const float*>(resampled.data()), out_samples));
                    }
                }
            }
        }
    }

    // Run OCR on decoded frame if available
    if (!rgb_frame.empty()) {
        ocr::TesseractBackend ocr;
        auto ocr_init = ocr.initialize("eng", "");
        if (ocr_init) {
            auto ocr_result = ocr.recognize(rgb_frame, 640, 480, 3);
            EXPECT_TRUE(ocr_result.ok());
            core::Logger::info("OCR lines: " + std::to_string(ocr_result.value().lines.size()), {});
        }
    }

    // Run ASR on decoded audio if available
    if (audio_buffer.sample_count() > 0) {
        asr::WhisperBackend asr;
        std::string model = TESTS_DIR "/models/ggml-tiny.bin";
        auto asr_init = asr.initialize(model, "en");
        if (asr_init) {
            auto asr_result = asr.transcribe(audio_buffer.float_data(), 16000);
            EXPECT_TRUE(asr_result.ok());
            core::Logger::info("ASR segments: " + std::to_string(asr_result.value().segments.size()), {});
        }
    }

    // Run embedding on a synthetic patch
    embedding::ONNXBackend embed;
    std::string model = TESTS_DIR "/models/tiny_image.onnx";
    embedding::EmbeddingConfig cfg{};
    cfg.visual_dim = 512;
    auto emb_init = embed.initialize(model, cfg);
    if (emb_init) {
        std::vector<std::vector<uint8_t>> patches = {std::vector<uint8_t>(64 * 64 * 3, 128)};
        auto emb_result = embed.encode_images(patches, 64, 64);
        EXPECT_TRUE(emb_result.ok());
        EXPECT_EQ(emb_result.value()[0].dim, 512);
    }

    core::Logger::info("End-to-end pipeline completed", {});
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
