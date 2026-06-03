#include <gtest/gtest.h>
#include <video2vec/sync/synchronizer.hpp>
#include <video2vec/sync/timestamp.hpp>

using namespace video2vec::sync;

TEST(SyncTimestamp, PtsToMs) {
    EXPECT_EQ(pts_to_ms(1000, 1, 1000), 1000);
    EXPECT_EQ(pts_to_ms(25, 1, 25), 1000);
}

TEST(SyncSynchronizer, Registration) {
    Synchronizer sync;
    sync.register_stream(0, 1, 1000);
    sync.register_stream(1, 1, 48000);
    EXPECT_EQ(sync.to_ms(0, 1000), 1000);
    EXPECT_EQ(sync.to_ms(1, 48000), 1000);
}

TEST(SyncSynchronizer, AlignNoDrift) {
    Synchronizer sync(20);
    auto report = sync.align(1000, 1005);
    EXPECT_EQ(report.drift_ms, 5);
    EXPECT_FALSE(report.corrected);
}

TEST(SyncSynchronizer, AlignWithDrift) {
    Synchronizer sync(20);
    auto report = sync.align(1000, 1050);
    EXPECT_EQ(report.drift_ms, 50);
    EXPECT_TRUE(report.corrected);
}

TEST(SyncSynchronizer, Snap) {
    Synchronizer sync(20);
    auto snapped = sync.snap(1050, 1000);
    EXPECT_EQ(snapped, 1000);
    auto ok = sync.snap(1010, 1000);
    EXPECT_EQ(ok, 1010);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
