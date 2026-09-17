#include <gtest/gtest.h>
#include <video2vec/windowing/windowing.hpp>

#include <cstdint>
#include <vector>

using namespace video2vec::windowing;

TEST(Windowing, BasicGeneration) {
    WindowingConfig cfg{};
    cfg.window_duration_ms = 45000;
    cfg.overlap_ms = 5000;
    auto windows = generate_windows(120000, cfg);
    ASSERT_GE(windows.size(), 2);
    EXPECT_EQ(windows[0].t0_ms, 0);
    EXPECT_EQ(windows[0].t1_ms, 45000);
    EXPECT_EQ(windows[1].t0_ms, 40000);
}

TEST(Windowing, NegativeOverlapIsClampedToContiguousWindows) {
    WindowingConfig cfg{};
    cfg.window_duration_ms = 45000;
    cfg.overlap_ms = -5000;  // used to create 5 s gaps between windows
    auto windows = generate_windows(120000, cfg);
    ASSERT_EQ(windows.size(), 3u);
    for (size_t i = 1; i < windows.size(); ++i) EXPECT_EQ(windows[i].t0_ms, windows[i - 1].t1_ms);
    EXPECT_EQ(windows.back().t1_ms, 120000);
}

TEST(Windowing, OverlapNotSmallerThanWindowStillAdvances) {
    WindowingConfig cfg{};
    cfg.window_duration_ms = 45000;
    cfg.overlap_ms = 60000;
    auto windows = generate_windows(90000, cfg);
    ASSERT_GE(windows.size(), 2u);
    for (size_t i = 1; i < windows.size(); ++i) EXPECT_GT(windows[i].t0_ms, windows[i - 1].t0_ms);
    EXPECT_EQ(windows.back().t1_ms, 90000);
}

TEST(Windowing, ValidateConfigRejectsBadValues) {
    WindowingConfig cfg{};
    EXPECT_TRUE(validate_config(cfg).ok());
    cfg.overlap_ms = -1;
    EXPECT_FALSE(validate_config(cfg).ok());
    cfg.overlap_ms = 45000;
    EXPECT_FALSE(validate_config(cfg).ok());
    cfg.overlap_ms = 44999;
    EXPECT_TRUE(validate_config(cfg).ok());
    cfg.window_duration_ms = 0;
    EXPECT_FALSE(validate_config(cfg).ok());
    EXPECT_TRUE(generate_windows(1000, cfg).empty());
    EXPECT_TRUE(generate_windows(0, WindowingConfig{}).empty());
}

TEST(Windowing, AssignIncludesFinalTimestamp) {
    WindowingConfig cfg{};
    cfg.window_duration_ms = 45000;
    cfg.overlap_ms = 5000;
    auto windows = generate_windows(120000, cfg);
    ASSERT_EQ(windows.size(), 3u);
    std::vector<int64_t> items = {0, 44999, 45000, 119999, 120000, 120001};
    auto assigned = assign_to_windows<int64_t>(items, windows, [](const int64_t& t) { return t; });
    ASSERT_EQ(assigned.size(), 3u);
    EXPECT_EQ(assigned[0], (std::vector<int64_t>{0, 44999}));
    EXPECT_EQ(assigned[1], (std::vector<int64_t>{44999, 45000}));   // overlap region belongs to both
    EXPECT_EQ(assigned[2], (std::vector<int64_t>{119999, 120000}));  // 120000 == total duration was dropped before
    auto none = assign_to_windows<int64_t>(items, {}, [](const int64_t& t) { return t; });
    EXPECT_TRUE(none.empty());
}

TEST(Windowing, Contains) {
    Window w{0, 45000, 0};
    EXPECT_TRUE(w.contains(1000));
    EXPECT_TRUE(w.contains(44999));
    EXPECT_FALSE(w.contains(45000));
    EXPECT_FALSE(w.contains(-1));
}

TEST(Windowing, Overlaps) {
    Window a{0, 45000, 0};
    Window b{40000, 85000, 1};
    EXPECT_TRUE(a.overlaps(b));
    Window c{50000, 95000, 2};
    EXPECT_TRUE(b.overlaps(c));
    Window d{90000, 120000, 3};
    EXPECT_FALSE(b.overlaps(d));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
