#include <gtest/gtest.h>
#include <video2vec/index/faiss_store.hpp>
#include <video2vec/query/query_engine.hpp>

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace video2vec;

namespace {
    index::VectorRecord rec(const std::string& id, std::vector<float> v, int64_t ts, const std::string& type = "asr",
                            const std::string& text = "") {
        index::VectorRecord r{};
        r.id = id; r.vector = std::move(v); r.ts_ms = ts; r.type = type; r.text = text;
        r.video_id = "vid"; r.window_id = 7; r.confidence = 0.5; r.width = 640; r.height = 480;
        r.bbox = {1.0f, 2.0f, 3.0f, 4.0f}; r.proof_uri = "proof://" + id;
        return r;
    }

    std::string temp_path(const char* name) {
        return std::string(::testing::TempDir()) + name;
    }

    // Deterministic 2-D "text encoder": known words map to axis vectors.
    class StubEmbedder : public embedding::IEmbeddingBackend {
    public:
        core::Result<void> initialize(const std::string&, const embedding::EmbeddingConfig&) override { return core::Result<void>(); }
        core::Result<std::vector<embedding::Embedding>> encode_images(std::span<const std::vector<uint8_t>>, int, int) override {
            return core::Result<std::vector<embedding::Embedding>>(core::Error::from_code(core::ErrorCode::Unsupported, "n/a"));
        }
        core::Result<std::vector<embedding::Embedding>> encode_text(const std::vector<std::string>& texts) override {
            std::vector<embedding::Embedding> out;
            for (const auto& t : texts) {
                embedding::Embedding e{};
                e.quant = embedding::Quantization::FP32;
                e.dim = 2;
                if (t.find("alpha") != std::string::npos) e.float_data = {1.0f, 0.0f};
                else if (t.find("beta") != std::string::npos) e.float_data = {0.0f, 1.0f};
                else e.float_data = {0.7f, 0.7f};
                out.push_back(e);
            }
            return core::Result<std::vector<embedding::Embedding>>(std::move(out));
        }
        void unload() override {}
        [[nodiscard]] bool is_loaded() const override { return true; }
        [[nodiscard]] std::string name() const override { return "stub"; }
    };
}

// ------------------------------------------------------------------
// FAISSStore
// ------------------------------------------------------------------
TEST(IndexStore, InitializeValidatesArguments) {
    index::FAISSStore store;
    EXPECT_FALSE(store.initialize("", 0, "cosine").ok());
    EXPECT_FALSE(store.initialize("", 4, "manhattan").ok());
    EXPECT_FALSE(store.add({}).ok());  // not initialized
    EXPECT_TRUE(store.initialize("", 2, "cosine").ok());
    EXPECT_TRUE(store.initialize("", 2, "l2").ok());
    EXPECT_TRUE(store.initialize("", 2, "inner_product").ok());
}

TEST(IndexStore, CosineSearchIsScaleInvariantAndOrdered) {
    index::FAISSStore store;
    ASSERT_TRUE(store.initialize("", 2, "cosine").ok());
    // "big" points the same way as the query but with a large norm; under
    // raw inner product it would dominate, under cosine it ties with "a".
    ASSERT_TRUE(store.add({rec("a", {1.0f, 0.0f}, 0), rec("b", {0.0f, 1.0f}, 100000),
                           rec("c", {0.9f, 0.1f}, 200000), rec("big", {50.0f, 0.0f}, 300000)}).ok());
    EXPECT_EQ(store.count(), 4u);
    index::SearchConfig cfg{};
    cfg.top_k = 10;
    cfg.temporal_rerank = false;
    auto r = store.search(std::vector<float>{2.0f, 0.0f}, cfg);
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r.value().size(), 4u);  // top_k larger than the store returns everything
    EXPECT_NEAR(r.value()[0].score, 1.0f, 1e-5);
    EXPECT_NEAR(r.value()[1].score, 1.0f, 1e-5);
    EXPECT_EQ(r.value()[2].id, "c");
    EXPECT_EQ(r.value()[3].id, "b");
    for (size_t i = 1; i < r.value().size(); ++i) EXPECT_GE(r.value()[i - 1].score, r.value()[i].score);
    EXPECT_EQ(r.value()[0].record.proof_uri.substr(0, 8), "proof://");
}

TEST(IndexStore, L2SearchRanksNearestFirst) {
    index::FAISSStore store;
    ASSERT_TRUE(store.initialize("", 2, "l2").ok());
    ASSERT_TRUE(store.add({rec("far", {10.0f, 10.0f}, 0), rec("near", {1.1f, 0.0f}, 0), rec("mid", {3.0f, 0.0f}, 0)}).ok());
    index::SearchConfig cfg{};
    cfg.top_k = 2;
    cfg.temporal_rerank = false;
    auto r = store.search(std::vector<float>{1.0f, 0.0f}, cfg);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().size(), 2u);
    EXPECT_EQ(r.value()[0].id, "near");  // was ranked worst-first (raw distance sorted descending)
    EXPECT_EQ(r.value()[1].id, "mid");
    EXPECT_GT(r.value()[0].score, r.value()[1].score);
}

TEST(IndexStore, RejectsBadDimensionsAndTopK) {
    index::FAISSStore store;
    ASSERT_TRUE(store.initialize("", 3, "cosine").ok());
    EXPECT_FALSE(store.add({rec("x", {1.0f, 0.0f}, 0)}).ok());  // wrong dimension is an error, not zero-padded
    EXPECT_EQ(store.count(), 0u);
    ASSERT_TRUE(store.add({rec("y", {1.0f, 0.0f, 0.0f}, 0)}).ok());
    index::SearchConfig cfg{};
    cfg.top_k = 0;
    EXPECT_FALSE(store.search(std::vector<float>{1.0f, 0.0f, 0.0f}, cfg).ok());
    cfg.top_k = -3;
    EXPECT_FALSE(store.search(std::vector<float>{1.0f, 0.0f, 0.0f}, cfg).ok());
    cfg.top_k = 1;
    EXPECT_FALSE(store.search(std::vector<float>{1.0f, 0.0f}, cfg).ok());
    EXPECT_TRUE(store.search(std::vector<float>{1.0f, 0.0f, 0.0f}, cfg).ok());
}

TEST(IndexStore, FiltersByTypeAndTime) {
    index::FAISSStore store;
    ASSERT_TRUE(store.initialize("", 2, "cosine").ok());
    ASSERT_TRUE(store.add({rec("a", {1.0f, 0.0f}, 1000, "asr"), rec("o", {1.0f, 0.0f}, 5000, "ocr"),
                           rec("late", {1.0f, 0.0f}, 90000, "asr")}).ok());
    index::SearchConfig cfg{};
    cfg.top_k = 10;
    cfg.temporal_rerank = false;
    cfg.type_filter = "ocr";
    auto r = store.search(std::vector<float>{1.0f, 0.0f}, cfg);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().size(), 1u);
    EXPECT_EQ(r.value()[0].id, "o");
    cfg.type_filter.clear();
    cfg.time_window_start_ms = 0;
    cfg.time_window_end_ms = 10000;
    r = store.search(std::vector<float>{1.0f, 0.0f}, cfg);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().size(), 2u);
}

TEST(IndexStore, TemporalRerankBoostIsBounded) {
    index::FAISSStore store;
    ASSERT_TRUE(store.initialize("", 2, "cosine").ok());
    std::vector<index::VectorRecord> cluster;
    for (int i = 0; i < 8; ++i) cluster.push_back(rec("c" + std::to_string(i), {1.0f, 0.0f}, 1000 + i * 100));
    cluster.push_back(rec("lonely", {1.0f, 0.0f}, 900000));
    ASSERT_TRUE(store.add(cluster).ok());
    index::SearchConfig cfg{};
    cfg.top_k = 20;
    cfg.temporal_rerank = true;
    auto r = store.search(std::vector<float>{1.0f, 0.0f}, cfg);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().size(), 9u);
    for (const auto& sr : r.value()) {
        EXPECT_LE(sr.score, 1.5f + 1e-5f);  // used to compound to ~17x
        EXPECT_GE(sr.score, 1.0f - 1e-5f);
    }
    EXPECT_EQ(r.value().back().id, "lonely");
}

TEST(IndexStore, PersistLoadRoundTripKeepsAllFieldsAndSearchWorks) {
    const std::string path = temp_path("video2vec_index_test.bin");
    {
        index::FAISSStore store;
        ASSERT_TRUE(store.initialize(path, 2, "cosine").ok());
        auto a = rec("a", {1.0f, 0.0f}, 1000, "asr", "hello alpha");
        auto b = rec("b", {0.0f, 1.0f}, 50000, "ocr", "slide beta");
        ASSERT_TRUE(store.add({a, b}).ok());
        ASSERT_TRUE(store.persist(path).ok());
    }
    index::FAISSStore loaded;
    ASSERT_TRUE(loaded.load(path).ok());
    EXPECT_EQ(loaded.count(), 2u);
    index::SearchConfig cfg{};
    cfg.top_k = 5;
    cfg.temporal_rerank = false;
    auto r = loaded.search(std::vector<float>{0.0f, 1.0f}, cfg);
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r.value().size(), 2u);  // load() used to leave the index empty: zero results
    const auto& top = r.value()[0].record;
    EXPECT_EQ(top.id, "b");
    EXPECT_EQ(top.video_id, "vid");
    EXPECT_EQ(top.window_id, 7);
    EXPECT_EQ(top.ts_ms, 50000);
    EXPECT_EQ(top.type, "ocr");
    EXPECT_EQ(top.text, "slide beta");
    EXPECT_EQ(top.bbox, (std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f}));
    EXPECT_DOUBLE_EQ(top.confidence, 0.5);
    EXPECT_EQ(top.width, 640);
    EXPECT_EQ(top.height, 480);
    EXPECT_EQ(top.proof_uri, "proof://b");  // evidence URIs survive a round trip now
    // Adding after load keeps records and index in step.
    ASSERT_TRUE(loaded.add({rec("c", {0.0f, 1.0f}, 60000)}).ok());
    r = loaded.search(std::vector<float>{0.0f, 1.0f}, cfg);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().size(), 3u);
    std::remove(path.c_str());
}

TEST(IndexStore, LoadRejectsGarbageTruncatedAndOldFiles) {
    const std::string path = temp_path("video2vec_index_bad.bin");
    {
        std::ofstream f(path, std::ios::binary);
        f << "this is not an index";
    }
    index::FAISSStore store;
    EXPECT_FALSE(store.load(path).ok());
    {
        // Old (version-less) layout: int32 dim, int32 metric len, ...
        std::ofstream f(path, std::ios::binary);
        int32_t dim = 2, len = 6;
        f.write(reinterpret_cast<const char*>(&dim), 4);
        f.write(reinterpret_cast<const char*>(&len), 4);
        f << "cosine";
    }
    EXPECT_FALSE(store.load(path).ok());
    {
        index::FAISSStore good;
        ASSERT_TRUE(good.initialize("", 2, "cosine").ok());
        ASSERT_TRUE(good.add({rec("a", {1.0f, 0.0f}, 0)}).ok());
        ASSERT_TRUE(good.persist(path).ok());
    }
    std::ifstream in(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    for (size_t len = 0; len < bytes.size(); len += 7) {
        std::ofstream f(path, std::ios::binary);
        f.write(bytes.data(), static_cast<std::streamsize>(len));
        f.close();
        EXPECT_FALSE(store.load(path).ok()) << "truncated to " << len;
    }
    EXPECT_FALSE(store.load("/nonexistent/dir/index.bin").ok());
    std::remove(path.c_str());
}

// ------------------------------------------------------------------
// QueryEngine
// ------------------------------------------------------------------
TEST(QueryEngine, ResultsAreScoreOrderedAndClamped) {
    auto store = std::make_shared<index::FAISSStore>();
    ASSERT_TRUE(store->initialize("", 2, "cosine").ok());
    // "beta" hit is earliest in time but should rank first for a beta query.
    ASSERT_TRUE(store->add({rec("a", {1.0f, 0.0f}, 300000, "asr", "talking about alpha"),
                            rec("b", {0.0f, 1.0f}, 1000, "asr", "talking about beta"),
                            rec("m", {0.6f, 0.8f}, 600000, "asr", "mixed")}).ok());
    query::QueryEngine engine(store, std::make_shared<StubEmbedder>());
    query::QueryRequest req{};
    req.text = "beta";
    req.top_k = 5;
    req.merge_by_time = true;  // the merge step used to leave results in time order
    auto r = engine.search(req);
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r.value().size(), 3u);
    EXPECT_EQ(r.value()[0].text, "talking about beta");
    for (size_t i = 1; i < r.value().size(); ++i) EXPECT_GE(r.value()[i - 1].score, r.value()[i].score);
    EXPECT_EQ(r.value()[0].window_t0_ms, 0);  // 1000 - 5000 clamps to zero instead of going negative
    EXPECT_EQ(r.value()[0].window_t1_ms, 6000);
    ASSERT_EQ(r.value()[0].evidence_uris.size(), 1u);
    EXPECT_EQ(r.value()[0].evidence_uris[0], "proof://b");
    req.top_k = 0;
    EXPECT_FALSE(engine.search(req).ok());
}

TEST(QueryEngine, MergeByTimeJoinsNeighboursWithoutDuplicatingText) {
    auto store = std::make_shared<index::FAISSStore>();
    ASSERT_TRUE(store->initialize("", 2, "cosine").ok());
    ASSERT_TRUE(store->add({rec("a1", {1.0f, 0.0f}, 10000, "asr", "alpha one"),
                            rec("a2", {1.0f, 0.0f}, 12000, "asr", "alpha two"),
                            rec("a3", {1.0f, 0.0f}, 14000, "asr", "alpha two"),
                            rec("far", {1.0f, 0.0f}, 500000, "asr", "alpha far")}).ok());
    query::QueryEngine engine(store, std::make_shared<StubEmbedder>());
    query::QueryRequest req{};
    req.text = "alpha";
    req.top_k = 10;
    req.merge_by_time = true;
    auto r = engine.search(req);
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value().size(), 2u);
    const auto& merged = r.value()[0].window_t0_ms < r.value()[1].window_t0_ms ? r.value()[0] : r.value()[1];
    EXPECT_EQ(merged.window_t0_ms, 5000);
    EXPECT_EQ(merged.window_t1_ms, 19000);
    EXPECT_EQ(merged.text, "alpha one alpha two");  // "alpha two" appears once
    EXPECT_EQ(merged.evidence_uris.size(), 3u);
}

TEST(QueryEngine, HybridSearchRewardsKeywordOverlap) {
    auto store = std::make_shared<index::FAISSStore>();
    ASSERT_TRUE(store->initialize("", 2, "cosine").ok());
    ASSERT_TRUE(store->add({rec("v", {0.7f, 0.7f}, 0, "asr", "nothing relevant here"),
                            rec("k", {0.7f, 0.7f}, 400000, "asr", "the gradient descent step")}).ok());
    query::QueryEngine engine(store, std::make_shared<StubEmbedder>());
    query::QueryRequest req{};
    req.text = "gradient descent";
    req.top_k = 5;
    req.merge_by_time = false;
    auto plain = engine.search(req);
    ASSERT_TRUE(plain.ok());
    ASSERT_EQ(plain.value().size(), 2u);
    EXPECT_NEAR(plain.value()[0].score, plain.value()[1].score, 1e-5);  // identical vectors
    auto hybrid = engine.search_hybrid(req, 0.5f);
    ASSERT_TRUE(hybrid.ok());
    ASSERT_EQ(hybrid.value().size(), 2u);
    EXPECT_EQ(hybrid.value()[0].text, "the gradient descent step");
    EXPECT_GT(hybrid.value()[0].score, hybrid.value()[1].score);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
