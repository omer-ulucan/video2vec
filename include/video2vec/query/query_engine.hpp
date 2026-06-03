#pragma once

#include "video2vec/core/result.hpp"
#include "video2vec/index/vector_store.hpp"
#include "video2vec/embedding/embedding_backend.hpp"
#include <memory>
#include <string>
#include <vector>

namespace video2vec::query {

struct QueryRequest {
    std::string text;
    int top_k = 8;
    bool merge_by_time = true;
    bool expand_context = false;
    int64_t context_ms_before = 5000;
    int64_t context_ms_after = 5000;
    std::string type_filter;
};

struct QueryResult {
    int64_t window_t0_ms = 0;
    int64_t window_t1_ms = 0;
    std::string text;
    float score = 0.0f;
    std::vector<std::string> evidence_uris;
    std::vector<uint8_t> evidence_png;
};

class QueryEngine {
public:
    QueryEngine(std::shared_ptr<index::IVectorStore> store, std::shared_ptr<embedding::IEmbeddingBackend> embedder);
    core::Result<std::vector<QueryResult>> search(const QueryRequest& request);
    core::Result<std::vector<QueryResult>> search_hybrid(const QueryRequest& request, float text_weight = 0.7f);
    core::Result<std::vector<QueryResult>> search_time_aware(const QueryRequest& request, int64_t target_time_ms);
private:
    std::shared_ptr<index::IVectorStore> store_;
    std::shared_ptr<embedding::IEmbeddingBackend> embedder_;
};

} // namespace video2vec::query
