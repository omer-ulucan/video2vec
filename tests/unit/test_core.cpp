#include <gtest/gtest.h>
#include <video2vec/core/result.hpp>
#include <video2vec/core/config.hpp>
#include <video2vec/core/thread_pool.hpp>
#include <video2vec/core/metrics.hpp>
#include <video2vec/core/cancellation.hpp>
#include <video2vec/core/memory.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

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

// ------------------------------------------------------------------
// Config
// ------------------------------------------------------------------
TEST(CoreConfig, BasicOperations) {
    ConfigNode node;
    node.set("key", std::string("value"));
    auto val = node["key"].get<std::string>();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), "value");
}

TEST(CoreConfig, MissingKeyThrowsConst) {
    const ConfigNode node;
    EXPECT_THROW((void)node["missing"], std::out_of_range);
}

TEST(CoreConfig, MissingKeyAutocreatesNonConst) {
    ConfigNode node;
    EXPECT_NO_THROW((void)node["missing"]);
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

TEST(CoreConfig, MergeOntoEmptyConfigAdoptsValues) {
    // A default-constructed Config is not a map; merge used to be a silent no-op.
    Config base;
    Config loaded = Config::from_string("a: 1\nnested:\n  b: 2\n");
    base.merge(loaded);
    EXPECT_TRUE(base.has("a"));
    EXPECT_TRUE(base.has("nested.b"));
    EXPECT_EQ(base.get_value("nested.b", 0), 2);
}

TEST(CoreConfig, Validate) {
    Config cfg;
    cfg.root().set("required_key", std::string("present"));
    EXPECT_NO_THROW(cfg.validate({"required_key"}));
    EXPECT_THROW(cfg.validate({"missing_key"}), std::runtime_error);
}

TEST(CoreConfig, GetValueReturnsDefaultWhenMissing) {
    Config cfg;
    cfg.set("present", std::string("yes"));
    EXPECT_EQ(cfg.get_value("absent", 5), 5);
    EXPECT_EQ(cfg.get_value("absent.deeper", 2.5), 2.5);
    EXPECT_EQ(cfg.get_value("absent", "fallback"), "fallback");
    EXPECT_EQ(cfg.get_value("present", "fallback"), "yes");
    EXPECT_EQ(cfg.get_value("present", 9), 9);  // wrong type falls back too
}

TEST(CoreConfig, GetValueConvertsArithmeticTypes) {
    Config cfg = Config::from_string("n: 7\nf: 1.5\nflag: true\n");
    EXPECT_EQ(cfg.get_value("n", 0), 7);                 // int from int64_t
    EXPECT_EQ(cfg.get_value<int64_t>("n", 0), 7);
    EXPECT_FLOAT_EQ(cfg.get_value("f", 0.0f), 1.5f);     // float from double
    EXPECT_DOUBLE_EQ(cfg.get_value("n", 0.0), 7.0);      // double from int64_t
    EXPECT_TRUE(cfg.get_value("flag", false));
}

TEST(CoreConfig, QuotedYamlScalarsStayStrings) {
    Config cfg = Config::from_string("version: \"1.0\"\nid: '007'\nplain: 1.0\ncount: 007\n");
    EXPECT_EQ(cfg.get("version").get<std::string>().value_or(""), "1.0");
    EXPECT_EQ(cfg.get("id").get<std::string>().value_or(""), "007");
    EXPECT_TRUE(cfg.get("plain").get<double>().has_value());
    EXPECT_EQ(cfg.get("count").get<int64_t>().value_or(-1), 7);
}

TEST(CoreConfig, ParseErrorsAreRuntimeErrors) {
    EXPECT_THROW(Config::from_string("a: [1, 2", "yaml"), std::runtime_error);
    EXPECT_THROW(Config::from_string("{\"a\": ", "json"), std::runtime_error);
    EXPECT_THROW(Config::from_yaml("/nonexistent/dir/config.yaml"), std::runtime_error);
    EXPECT_THROW(Config::from_json("/nonexistent/dir/config.json"), std::runtime_error);
}

TEST(CoreConfig, EmptyPathIsRejected) {
    Config cfg;
    EXPECT_FALSE(cfg.has(""));
    EXPECT_THROW(cfg.set("", std::string("x")), std::invalid_argument);
    EXPECT_THROW((void)cfg.get(""), std::out_of_range);
}

// ------------------------------------------------------------------
// ThreadPool
// ------------------------------------------------------------------
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

TEST(CoreThreadPool, CreateDestroyStressDoesNotHang) {
    // Rapid construct/submit/destroy cycles used to hang in join() because
    // shutdown() flipped stop_ outside the queue mutex (lost wakeup).
    std::atomic<int> executed{0};
    for (int i = 0; i < 200; ++i) {
        ThreadPool pool(4);
        for (int j = 0; j < 8; ++j) pool.submit([&executed]() { executed.fetch_add(1); });
    }
    EXPECT_EQ(executed.load(), 200 * 8);  // queued tasks run to completion before shutdown
}

TEST(CoreThreadPool, ShutdownIsIdempotentAndRejectsSubmit) {
    ThreadPool pool(2);
    pool.shutdown();
    pool.shutdown();
    EXPECT_EQ(pool.size(), 0u);
    EXPECT_THROW(pool.submit([]() { return 1; }), std::runtime_error);
}

// ------------------------------------------------------------------
// Metrics
// ------------------------------------------------------------------
TEST(CoreMetrics, Counter) {
    Counter c;
    c.increment(5);
    EXPECT_EQ(c.value(), 5);
}

TEST(CoreMetrics, HistogramPercentileClampsFraction) {
    Histogram h;
    for (int i = 1; i <= 10; ++i) h.observe(i);
    EXPECT_EQ(h.percentile(0.5), 6.0);
    EXPECT_EQ(h.percentile(1.5), 10.0);   // out-of-range p clamps instead of indexing garbage
    EXPECT_EQ(h.percentile(-1.0), 1.0);
    EXPECT_EQ(h.percentile(1.0), 10.0);
}

// ------------------------------------------------------------------
// CancellationToken
// ------------------------------------------------------------------
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

TEST(CoreCancellation, CallbackFiresOnceEvenAfterDeadlineExpiry) {
    int calls = 0;
    CancellationToken token(std::chrono::milliseconds(10));
    token.on_cancel([&calls]() { ++calls; });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_TRUE(token.is_cancelled());
    EXPECT_EQ(calls, 0);  // deadline expiry alone does not invoke the callback
    token.cancel();
    EXPECT_EQ(calls, 1);  // an explicit cancel afterwards still delivers it
    token.cancel();
    EXPECT_EQ(calls, 1);
}

TEST(CoreCancellation, CallbackRegisteredAfterCancelRunsOnceOutsideLock) {
    CancellationToken token;
    token.cancel();
    int calls = 0;
    // The callback re-enters the token; this deadlocked when the callback
    // was invoked while holding the token's mutex.
    token.on_cancel([&]() { ++calls; EXPECT_FALSE(token.sleep_for(std::chrono::milliseconds(1))); });
    EXPECT_EQ(calls, 1);
    token.cancel();
    EXPECT_EQ(calls, 1);
}

TEST(CoreCancellation, SleepFor) {
    CancellationToken token;
    bool slept = token.sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(slept);
    token.cancel();
    slept = token.sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(slept);
}

TEST(CoreCancellation, SleepForHonorsDeadline) {
    CancellationToken token(std::chrono::milliseconds(20));
    auto start = std::chrono::steady_clock::now();
    bool slept = token.sleep_for(std::chrono::milliseconds(2000));
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_FALSE(slept);
    EXPECT_LT(elapsed, std::chrono::milliseconds(1000));
    EXPECT_TRUE(token.is_cancelled());
}

TEST(CoreCancellation, SleepForWakesOnCancelFromOtherThread) {
    CancellationToken token;
    std::thread canceller([&token]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        token.cancel();
    });
    auto start = std::chrono::steady_clock::now();
    bool slept = token.sleep_for(std::chrono::milliseconds(2000));
    auto elapsed = std::chrono::steady_clock::now() - start;
    canceller.join();
    EXPECT_FALSE(slept);
    EXPECT_LT(elapsed, std::chrono::milliseconds(1000));
}

// ------------------------------------------------------------------
// AlignedBuffer / MemoryTracker
// ------------------------------------------------------------------
TEST(CoreMemory, AlignedBuffer) {
    AlignedBuffer<float> buf(64, 64);
    EXPECT_NE(buf.data(), nullptr);
    EXPECT_EQ(buf.size(), 64);
    buf.span()[0] = 1.0f;
    EXPECT_EQ(buf.span()[0], 1.0f);
}

TEST(CoreMemory, AlignedBufferSizeNotMultipleOfAlignment) {
    // 10 floats = 40 bytes at 64-byte alignment: aligned_alloc requires the
    // size to be a multiple of the alignment, so this must be rounded up.
    AlignedBuffer<float> buf(10, 64);
    ASSERT_NE(buf.data(), nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(buf.data()) % 64, 0u);
    EXPECT_EQ(buf.size(), 10u);
    for (size_t i = 0; i < buf.size(); ++i) buf.span()[i] = static_cast<float>(i);
    EXPECT_EQ(buf.span()[9], 9.0f);
    AlignedBuffer<uint8_t> tiny(1, 4096);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(tiny.data()) % 4096, 0u);
    AlignedBuffer<double> empty(0);
    EXPECT_EQ(empty.size(), 0u);
}

TEST(CoreMemory, AlignedBufferRejectsBadAlignment) {
    EXPECT_THROW(AlignedBuffer<float>(8, 0), std::invalid_argument);
    EXPECT_THROW(AlignedBuffer<float>(8, 48), std::invalid_argument);
}

TEST(CoreMemory, AlignedBufferMove) {
    AlignedBuffer<float> a(64);
    float* ptr = a.data();
    AlignedBuffer<float> b = std::move(a);
    EXPECT_EQ(b.data(), ptr);
    EXPECT_EQ(a.data(), nullptr);
    AlignedBuffer<float> c(8);
    c = std::move(b);
    EXPECT_EQ(c.data(), ptr);
    EXPECT_EQ(c.size(), 64u);
    EXPECT_EQ(b.data(), nullptr);
    EXPECT_EQ(b.size(), 0u);
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

TEST(CoreMemory, TrackerReRegisterDoesNotInflateActive) {
    auto& tracker = MemoryTracker::instance();
    tracker.reset();
    int x = 0;
    tracker.allocate(&x, 100, "first");
    tracker.allocate(&x, 50, "second");
    EXPECT_EQ(tracker.total_active(), 50);
    EXPECT_EQ(tracker.total_allocated(), 150);
    tracker.deallocate(&x);
    EXPECT_EQ(tracker.total_active(), 0);
    tracker.reset();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
