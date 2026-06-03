#include <gtest/gtest.h>
#include <video2vec/ffmpeg/demuxer.hpp>

#include <video2vec/vision/frame_selector.hpp>
#include <video2vec/vision/patch_extractor.hpp>
#include <video2vec/flatbuffers/packager.hpp>

using namespace video2vec;

TEST(Pipeline, PackageRoundTrip) {
    std::vector<flatbuffers::PackagedWindow> windows;
    flatbuffers::PackagedWindow w{};
    w.t0_ms = 0; w.t1_ms = 45000; w.index = 0;
    w.transcript = "hello world";
    windows.push_back(w);
    auto packed = flatbuffers::package_windows(windows);
    ASSERT_TRUE(packed.ok());
    auto unpacked = flatbuffers::unpack_windows(packed.value());
    ASSERT_TRUE(unpacked.ok());
    ASSERT_EQ(unpacked.value().size(), 1);
    EXPECT_EQ(unpacked.value()[0].transcript, "hello world");
}

TEST(Pipeline, FrameSelectionDedup) {
    vision::Frame f1{0, 640, 480, std::vector<uint8_t>(640 * 480 * 3, 128), true, 5.0, 0.0, 0.0};
    vision::Frame f2{1000, 640, 480, std::vector<uint8_t>(640 * 480 * 3, 128), false, 4.0, 0.0, 0.0};
    std::vector<vision::Frame> frames = {f1, f2};
    vision::SelectionConfig cfg{};
    cfg.max_frames_per_window = 6;
    cfg.min_frames_per_window = 1;
    cfg.ssim_threshold = 0.99;
    auto selected = vision::select_frames(frames, cfg);
    EXPECT_LE(selected.size(), 1);
}

TEST(Pipeline, PatchExtraction) {
    vision::Frame frame{};
    frame.width = 640; frame.height = 480;
    frame.rgb_data.resize(640 * 480 * 3, 200);
    frame.pts_ms = 1000;
    std::vector<ocr::OCRLine> ocr_lines;
    ocr::OCRLine line{};
    line.bbox = {100, 100, 200, 50};
    line.text = "Test"; line.confidence = 0.95;
    ocr_lines.push_back(line);
    vision::PatchConfig cfg{};
    cfg.min_patches_per_frame = 2;
    auto patches = vision::extract_patches(frame, ocr_lines, cfg);
    EXPECT_GE(patches.size(), 1);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
