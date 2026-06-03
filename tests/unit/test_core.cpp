#include <gtest/gtest.h>
#include <video2vec/core/result.hpp>
#include <video2vec/core/config.hpp>
#include <video2vec/core/thread_pool.hpp>
#include <video2vec/core/metrics.hpp>
#include <video2vec/core/cancellation.hpp>

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

TEST(CoreConfig, BasicOperations) {
    ConfigNode node;
    node.set("key", std::string("value"));
    auto val = node["key"].get<std::string>();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), "value");
}

TEST(CoreThreadPool, Submit) {
    ThreadPool pool(2);
    auto future = pool.submit([]() { return 42; });
    EXPECT_EQ(future.get(), 42);
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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
