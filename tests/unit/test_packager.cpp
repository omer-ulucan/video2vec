#include <gtest/gtest.h>
#include <video2vec/flatbuffers/packager.hpp>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace video2vec;

namespace {
    flatbuffers::PackagedWindow sample_window() {
        flatbuffers::PackagedWindow w{};
        w.t0_ms = 45000; w.t1_ms = 90000; w.index = 3;
        w.transcript = "hello world, ünïcödé";
        asr::ASRWord word{};
        word.t_ms = 45120; word.dt_ms = 380; word.text = "hello"; word.confidence = 0.87;
        w.words.push_back(word);
        word.t_ms = 45500; word.dt_ms = 400; word.text = "world"; word.confidence = 0.42;
        w.words.push_back(word);
        vision::Frame frame{};
        frame.pts_ms = 46000; frame.width = 4; frame.height = 2; frame.is_keyframe = true;
        frame.entropy = 6.5; frame.ocr_density = 0.25; frame.score = 0.9;
        frame.rgb_data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
        w.frames.push_back(frame);
        ocr::OCRLine line{};
        line.bbox = {10, 20, 300, 40}; line.text = "Slide title"; line.confidence = 0.93; line.pii_flagged = true;
        w.ocr_lines.push_back(line);
        embedding::Embedding e8{};
        e8.type = embedding::EmbeddingType::Patch; e8.quant = embedding::Quantization::INT8; e8.dim = 4;
        e8.int8_scale = 2.5f; e8.int8_data = {127, -128, 0, 64};
        w.embeddings.push_back(e8);
        embedding::Embedding e32{};
        e32.type = embedding::EmbeddingType::Global; e32.quant = embedding::Quantization::FP32; e32.dim = 3;
        e32.float_data = {0.1f, -0.2f, 0.3f};
        w.embeddings.push_back(e32);
        w.processing_time_ms = 1234;
        return w;
    }

    void put_u32(std::vector<uint8_t>& buf, size_t offset, uint32_t val) {
        for (int i = 0; i < 4; ++i) buf[offset + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
    }
}

TEST(Packager, RoundTripPreservesEveryField) {
    std::vector<flatbuffers::PackagedWindow> windows = {sample_window(), flatbuffers::PackagedWindow{}};
    auto packed = flatbuffers::package_windows(windows, "9.9.9");
    ASSERT_TRUE(packed.ok());
    auto unpacked = flatbuffers::unpack_windows(packed.value());
    ASSERT_TRUE(unpacked.ok()) << unpacked.error().message;
    ASSERT_EQ(unpacked.value().size(), 2u);
    const auto& w = unpacked.value()[0];
    const auto& src = windows[0];
    EXPECT_EQ(w.t0_ms, src.t0_ms);
    EXPECT_EQ(w.t1_ms, src.t1_ms);
    EXPECT_EQ(w.index, src.index);
    EXPECT_EQ(w.transcript, src.transcript);
    EXPECT_EQ(w.processing_time_ms, 1234);

    ASSERT_EQ(w.words.size(), 2u);
    EXPECT_EQ(w.words[0].text, "hello");
    EXPECT_EQ(w.words[0].t_ms, 45120);
    EXPECT_EQ(w.words[0].dt_ms, 380);
    EXPECT_DOUBLE_EQ(w.words[0].confidence, 0.87);  // was ~1.0 after the 4-byte memcpy of a double
    EXPECT_DOUBLE_EQ(w.words[1].confidence, 0.42);

    ASSERT_EQ(w.frames.size(), 1u);
    EXPECT_EQ(w.frames[0].pts_ms, 46000);
    EXPECT_EQ(w.frames[0].width, 4);
    EXPECT_EQ(w.frames[0].height, 2);
    EXPECT_TRUE(w.frames[0].is_keyframe);
    EXPECT_DOUBLE_EQ(w.frames[0].entropy, 6.5);
    EXPECT_DOUBLE_EQ(w.frames[0].ocr_density, 0.25);
    EXPECT_DOUBLE_EQ(w.frames[0].score, 0.9);
    EXPECT_EQ(w.frames[0].rgb_data, src.frames[0].rgb_data);

    ASSERT_EQ(w.ocr_lines.size(), 1u);
    EXPECT_EQ(w.ocr_lines[0].bbox.x, 10);
    EXPECT_EQ(w.ocr_lines[0].bbox.h, 40);
    EXPECT_EQ(w.ocr_lines[0].text, "Slide title");
    EXPECT_DOUBLE_EQ(w.ocr_lines[0].confidence, 0.93);
    EXPECT_TRUE(w.ocr_lines[0].pii_flagged);

    ASSERT_EQ(w.embeddings.size(), 2u);
    EXPECT_EQ(w.embeddings[0].type, embedding::EmbeddingType::Patch);
    EXPECT_EQ(w.embeddings[0].quant, embedding::Quantization::INT8);
    EXPECT_EQ(w.embeddings[0].dim, 4);
    EXPECT_FLOAT_EQ(w.embeddings[0].int8_scale, 2.5f);
    EXPECT_EQ(w.embeddings[0].int8_data, src.embeddings[0].int8_data);
    EXPECT_EQ(w.embeddings[1].quant, embedding::Quantization::FP32);
    EXPECT_EQ(w.embeddings[1].float_data, src.embeddings[1].float_data);

    const auto& empty = unpacked.value()[1];
    EXPECT_TRUE(empty.transcript.empty());
    EXPECT_TRUE(empty.words.empty() && empty.frames.empty() && empty.ocr_lines.empty() && empty.embeddings.empty());
}

TEST(Packager, EmptyBatchRoundTrips) {
    auto packed = flatbuffers::package_windows({});
    ASSERT_TRUE(packed.ok());
    auto unpacked = flatbuffers::unpack_windows(packed.value());
    ASSERT_TRUE(unpacked.ok());
    EXPECT_TRUE(unpacked.value().empty());
}

TEST(Packager, EveryTruncationIsAnErrorNotACrash) {
    auto packed = flatbuffers::package_windows({sample_window()});
    ASSERT_TRUE(packed.ok());
    const auto& full = packed.value();
    for (size_t len = 0; len < full.size(); ++len) {
        std::vector<uint8_t> cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(len));
        auto r = flatbuffers::unpack_windows(cut);
        ASSERT_FALSE(r.ok()) << "truncated to " << len << " bytes was accepted";
        EXPECT_EQ(r.error().code, core::make_error_code(core::ErrorCode::DecodeError));
    }
}

TEST(Packager, RejectsBadMagicVersionAndCounts) {
    auto packed = flatbuffers::package_windows({sample_window()});
    ASSERT_TRUE(packed.ok());
    auto bad_magic = packed.value();
    bad_magic[0] ^= 0xFF;
    EXPECT_FALSE(flatbuffers::unpack_windows(bad_magic).ok());

    auto bad_version = packed.value();
    put_u32(bad_version, 4, flatbuffers::kVecFormatVersion + 1);
    auto r = flatbuffers::unpack_windows(bad_version);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("format version"), std::string::npos);

    // A window count claiming billions of windows in a few hundred bytes.
    auto huge_count = packed.value();
    const size_t count_offset = 4 + 4 + 4 + std::string(VIDEO2VEC_VERSION_STRING).size();
    put_u32(huge_count, count_offset, 0xFFFFFFFFu);
    EXPECT_FALSE(flatbuffers::unpack_windows(huge_count).ok());

    // A string length pointing far past the end of the buffer.
    auto huge_string = packed.value();
    put_u32(huge_string, count_offset + 4 + 8 + 8 + 4, 0x7FFFFFFFu);
    EXPECT_FALSE(flatbuffers::unpack_windows(huge_string).ok());

    // The old (version 1) layout is refused rather than misparsed.
    std::vector<uint8_t> v1 = {0x21, 0x43, 0x45, 0x56, 5, 0, 0, 0, '0', '.', '1', '.', '0', 0, 0, 0, 0};
    EXPECT_FALSE(flatbuffers::unpack_windows(v1).ok());
}

TEST(Packager, WriteIndexReportsUnwritablePath) {
    auto r = flatbuffers::write_index({sample_window()}, "/nonexistent-dir/index.tsv");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::make_error_code(core::ErrorCode::IoError));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
