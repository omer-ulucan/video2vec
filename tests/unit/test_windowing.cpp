#include <gtest/gtest.h>
#include <video2vec/windowing/windowing.hpp>

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
