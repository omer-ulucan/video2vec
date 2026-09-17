#include <gtest/gtest.h>
#include <video2vec/vision/frame_selector.hpp>
#include <video2vec/vision/patch_extractor.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace video2vec;

namespace {
    vision::Frame make_frame(int64_t pts, int w, int h, uint8_t fill) {
        vision::Frame f{};
        f.pts_ms = pts;
        f.width = w;
        f.height = h;
        f.rgb_data.assign(static_cast<size_t>(w) * h * 3, fill);
        return f;
    }

    // Deterministic pseudo-noise so entropy is high without <random>.
    vision::Frame make_noisy_frame(int64_t pts, int w, int h, uint32_t seed) {
        vision::Frame f = make_frame(pts, w, h, 0);
        uint32_t x = seed;
        for (auto& b : f.rgb_data) {
            x = x * 1664525u + 1013904223u;
            b = static_cast<uint8_t>(x >> 24);
        }
        return f;
    }

    uint32_t read_u32be(const std::vector<uint8_t>& v, size_t off) {
        return (uint32_t(v[off]) << 24) | (uint32_t(v[off + 1]) << 16) | (uint32_t(v[off + 2]) << 8) | uint32_t(v[off + 3]);
    }
}

// ------------------------------------------------------------------
// Bounds: every function that walks width*height*3 bytes used to trust the
// dimensions and read past a shorter buffer.
// ------------------------------------------------------------------
TEST(VisionBounds, EntropyAndSsimRejectTruncatedBuffers) {
    std::vector<uint8_t> truncated(10, 128);
    EXPECT_EQ(vision::compute_entropy(truncated, 640, 480), 0.0);
    EXPECT_EQ(vision::compute_entropy(truncated, 0, 480), 0.0);
    std::vector<uint8_t> other(10, 128);
    EXPECT_EQ(vision::compute_ssim(truncated, other, 640, 480), 0.0);
    auto ok = make_frame(0, 8, 8, 100);
    EXPECT_GT(vision::compute_ssim(ok.rgb_data, ok.rgb_data, 8, 8), 0.99);
}

TEST(VisionBounds, CropRejectsTruncatedSourceAndClampsRegion) {
    std::vector<uint8_t> truncated(10, 128);
    EXPECT_TRUE(vision::crop_rgb(truncated, 640, 480, 0, 0, 10, 10).empty());
    auto f = make_frame(0, 16, 8, 7);
    auto crop = vision::crop_rgb(f.rgb_data, 16, 8, 12, 4, 100, 100);  // clamps to 4x4
    EXPECT_EQ(crop.size(), 4u * 4u * 3u);
    EXPECT_TRUE(vision::crop_rgb(f.rgb_data, 16, 8, 16, 0, 4, 4).empty());
    EXPECT_TRUE(vision::crop_rgb(f.rgb_data, 16, 8, 0, 0, 0, 4).empty());
}

// ------------------------------------------------------------------
// encode_png must return a real PNG (or nothing), never raw RGB.
// ------------------------------------------------------------------
TEST(VisionPng, OutputIsAValidPngContainer) {
    auto f = make_noisy_frame(0, 37, 21, 7);
    auto png = vision::encode_png(f.rgb_data, 37, 21);
    ASSERT_GE(png.size(), 8u + 25u + 12u + 12u);
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    EXPECT_EQ(std::memcmp(png.data(), sig, 8), 0);
    EXPECT_EQ(read_u32be(png, 8), 13u);                       // IHDR length
    EXPECT_EQ(std::memcmp(png.data() + 12, "IHDR", 4), 0);
    EXPECT_EQ(read_u32be(png, 16), 37u);                      // width
    EXPECT_EQ(read_u32be(png, 20), 21u);                      // height
    EXPECT_EQ(png[24], 8);                                    // bit depth
    EXPECT_EQ(png[25], 2);                                    // truecolour
    EXPECT_EQ(std::memcmp(png.data() + png.size() - 8, "IEND", 4), 0);
    EXPECT_NE(png, f.rgb_data);
    EXPECT_TRUE(vision::encode_png(std::vector<uint8_t>(5, 0), 37, 21).empty());
    EXPECT_TRUE(vision::encode_png(f.rgb_data, 0, 21).empty());
}

TEST(VisionResize, BilinearResizeKeepsFlatColourAndDimensions) {
    auto f = make_frame(0, 30, 20, 77);
    auto small = vision::resize_rgb(f.rgb_data, 30, 20, 8, 5);
    ASSERT_EQ(small.size(), 8u * 5u * 3u);
    for (auto b : small) EXPECT_EQ(b, 77);
    auto big = vision::resize_rgb(f.rgb_data, 30, 20, 64, 64);
    ASSERT_EQ(big.size(), 64u * 64u * 3u);
    EXPECT_EQ(big[0], 77);
    EXPECT_TRUE(vision::resize_rgb(f.rgb_data, 30, 20, 0, 5).empty());
    EXPECT_TRUE(vision::resize_rgb(std::vector<uint8_t>(3, 0), 30, 20, 8, 5).empty());
}

// ------------------------------------------------------------------
// Frame selection ranks by a score that is actually computed.
// ------------------------------------------------------------------
TEST(VisionSelect, HigherEntropyFrameWinsWhenScoresUnset) {
    auto flat = make_frame(0, 64, 48, 128);       // entropy 0
    auto noisy = make_noisy_frame(1000, 64, 48, 3); // entropy ~8
    vision::SelectionConfig cfg{};
    cfg.max_frames_per_window = 1;
    cfg.min_frames_per_window = 1;
    auto selected = vision::select_frames({flat, noisy}, cfg);
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected[0].pts_ms, 1000);
    EXPECT_GT(selected[0].entropy, 7.0);
    EXPECT_GT(selected[0].score, 0.5);
}

TEST(VisionSelect, DuplicatesDroppedAndOutputInTimeOrder) {
    auto a = make_noisy_frame(2000, 32, 32, 11);
    auto b = a;
    b.pts_ms = 0;  // identical pixels, earlier time
    auto c = make_noisy_frame(1000, 32, 32, 99);
    vision::SelectionConfig cfg{};
    cfg.max_frames_per_window = 6;
    cfg.min_frames_per_window = 1;
    cfg.ssim_threshold = 0.99;
    auto selected = vision::select_frames({a, b, c}, cfg);
    ASSERT_EQ(selected.size(), 2u);
    EXPECT_LT(selected[0].pts_ms, selected[1].pts_ms);
    EXPECT_EQ(vision::select_frames({}, cfg).size(), 0u);
}

TEST(VisionSelect, DeduplicateUsesEmbeddingOncePerFrame) {
    auto a = make_noisy_frame(0, 16, 16, 5);
    auto b = a; b.pts_ms = 1;
    auto c = a; c.pts_ms = 2;
    int calls = 0;
    auto embed = [&calls](const vision::Frame&) { ++calls; return std::vector<float>{1.0f, 0.5f}; };  // identical embeddings
    auto kept = vision::deduplicate_frames({a, b, c}, 0.99, 0.5, embed);
    EXPECT_EQ(kept.size(), 1u);
    EXPECT_LE(calls, 3);  // one embedding per frame, not one per comparison
}

// ------------------------------------------------------------------
// Patch extraction: grid coverage, no empty patches, fills to the minimum.
// ------------------------------------------------------------------
TEST(VisionPatches, GridCoversWholeFrameAndFillsToMinimum) {
    auto f = make_noisy_frame(0, 640, 481, 1);  // 481 is not divisible by 3
    vision::PatchConfig cfg{};
    cfg.min_patches_per_frame = 9;
    cfg.max_patches_per_frame = 9;
    auto patches = vision::extract_patches(f, {}, cfg);
    ASSERT_EQ(patches.size(), 9u);
    std::vector<uint8_t> covered(640 * 481, 0);
    for (const auto& p : patches) {
        EXPECT_GT(p.width, 0);
        EXPECT_GT(p.height, 0);
        EXPECT_FALSE(p.png_data.empty());
        EXPECT_EQ(p.source, "entropy");
        EXPECT_GE(p.score, 0.0);
        EXPECT_LE(p.score, 1.0);
        for (int y = p.y; y < p.y + p.height; ++y)
            for (int x = p.x; x < p.x + p.width; ++x) covered[static_cast<size_t>(y) * 640 + x] = 1;
    }
    EXPECT_EQ(std::count(covered.begin(), covered.end(), 1), 640 * 481);
}

TEST(VisionPatches, OcrPatchesPlusGridStopAtMinimum) {
    auto f = make_noisy_frame(0, 640, 480, 2);
    std::vector<ocr::OCRLine> lines(3);
    for (int i = 0; i < 3; ++i) { lines[i].bbox = {20, 20 + i * 100, 200, 40}; lines[i].text = "x"; lines[i].confidence = 0.9; }
    vision::PatchConfig cfg{};
    cfg.min_patches_per_frame = 4;
    cfg.max_patches_per_frame = 8;
    auto patches = vision::extract_patches(f, lines, cfg);
    EXPECT_EQ(patches.size(), 4u);  // 3 OCR + 1 grid fill, not filled to max
    EXPECT_EQ(patches[0].source, "ocr");
    EXPECT_DOUBLE_EQ(patches[0].score, 0.9);
}

TEST(VisionPatches, TinyOrTruncatedFramesProduceNoEmptyPatches) {
    auto tiny = make_noisy_frame(0, 2, 2, 3);
    vision::PatchConfig cfg{};
    cfg.min_patches_per_frame = 4;
    auto patches = vision::extract_patches(tiny, {}, cfg);
    ASSERT_EQ(patches.size(), 1u);
    EXPECT_EQ(patches[0].width, 2);
    EXPECT_EQ(patches[0].height, 2);
    EXPECT_FALSE(patches[0].png_data.empty());

    vision::Frame truncated{};
    truncated.width = 640; truncated.height = 480;
    truncated.rgb_data.assign(100, 0);
    EXPECT_TRUE(vision::extract_patches(truncated, {}, cfg).empty());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
