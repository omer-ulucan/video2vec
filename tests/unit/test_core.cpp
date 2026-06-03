#include <gtest/gtest.h>
#include <video2vec/core/result.hpp>
#include <video2vec/core/config.hpp>
#include <video2vec/core/thread_pool.hpp>
#include <video2vec/core/metrics.hpp>
#include <video2vec/core/cancellation.hpp>
#include <video2vec/core/memory.hpp>

using namespace video2vec::core;

TEST(CoreResult, Success) {
    Result<int> r(42);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value(), 42);
}

TEST(CoreResult, Error) {
    Result<int> r(Error::from_code(ErrorCode::InvalidArgument, "bad input"));
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, make_error_code(ErrorCode::InvalidArgument));
}

TEST(CoreResult, Map) {
    Result<int> r(21);
    auto mapped = r.map([](int x) { return x * 2; });
    ASSERT_TRUE(mapped.ok());
    EXPECT_EQ(mapped.value(), 42);
}

TEST(CoreResult, ValueOr) {
    Result<int> ok(7);
    EXPECT_EQ(ok.value_or(3), 7);
    Result<int> err(Error::from_code(ErrorCode::InvalidArgument, "x"));
    EXPECT_EQ(err.value_or(3), 3);
}

TEST(CoreResult, VoidResult) {
    Result<void> ok;
    ASSERT_TRUE(ok.ok());
    Result<void> err(Error::from_code(ErrorCode::IoError, "fail"));
    ASSERT_FALSE(err.ok());
}

TEST(CoreConfig, BasicOperations) {
    ConfigNode node;
    node.set("key", std::string("value"));
    auto val = node["key"].get<std::string>();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), "value");
}

TEST(CoreConfig, MissingKeyThrowsConst) {
    const ConfigNode node;
    EXPECT_THROW(node["missing"], std::out_of_range);
}

TEST(CoreConfig, MissingKeyAutocreatesNonConst) {
    ConfigNode node;
    EXPECT_NO_THROW(node["missing"]);
    EXPECT_TRUE(node.has("missing"));
}

TEST(CoreConfig, Merge) {
    ConfigNode a;
    a.set("x", std::string("1"));
    ConfigNode b;
    b.set("y", std::string("2"));
    a.merge(b);
    EXPECT_TRUE(a.has("x"));
    EXPECT_TRUE(a.has("y"));
}

TEST(CoreConfig, Validate) {
    Config cfg;
    cfg.root().set("required_key", std::string("present"));
    EXPECT_NO_THROW(cfg.validate({"required_key"}));
    EXPECT_THROW(cfg.validate({"missing_key"}), std::runtime_error);
}

TEST(CoreThreadPool, Submit) {
    ThreadPool pool(2);
    auto future = pool.submit([]() { return 42; });
    EXPECT_EQ(future.get(), 42);
}

TEST(CoreThreadPool, ExceptionDoesNotLeakActiveCount) {
    ThreadPool pool(2);
    auto f = pool.submit([]() { throw std::runtime_error("boom"); });
    EXPECT_THROW(f.get(), std::runtime_error);
    // Give a moment for active count to settle
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(pool.active(), 0);
}

TEST(CoreMetrics, Counter) {
    Counter c;
    c.increment(5);
    EXPECT_EQ(c.value(), 5);
}

TEST(CoreCancellation, Basic) {
    CancellationToken token;
    EXPECT_FALSE(token.is_cancelled());
    token.cancel();
    EXPECT_TRUE(token.is_cancelled());
}

TEST(CoreCancellation, Timeout) {
    CancellationToken token(std::chrono::milliseconds(10));
    EXPECT_FALSE(token.is_cancelled());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(token.is_cancelled());
}

TEST(CoreCancellation, Callback) {
    bool called = false;
    CancellationToken token;
    token.on_cancel([&called]() { called = true; });
    token.cancel();
    EXPECT_TRUE(called);
}

TEST(CoreCancellation, SleepFor) {
    CancellationToken token;
    bool slept = token.sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(slept);
    token.cancel();
    slept = token.sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(slept);
}

TEST(CoreMemory, AlignedBuffer) {
    AlignedBuffer<float> buf(64, 64);
    EXPECT_NE(buf.data(), nullptr);
    EXPECT_EQ(buf.size(), 64);
    buf.span()[0] = 1.0f;
    EXPECT_EQ(buf.span()[0], 1.0f);
}

TEST(CoreMemory, AlignedBufferMove) {
    AlignedBuffer<float> a(64);
    float* ptr = a.data();
    AlignedBuffer<float> b = std::move(a);
    EXPECT_EQ(b.data(), ptr);
    EXPECT_EQ(a.data(), nullptr);
}

TEST(CoreMemory, TrackerBasic) {
    auto& tracker = MemoryTracker::instance();
    tracker.reset();
    int x = 0;
    tracker.allocate(&x, 100, "test");
    EXPECT_EQ(tracker.total_active(), 100);
    tracker.deallocate(&x);
    EXPECT_EQ(tracker.total_active(), 0);
    tracker.reset();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
