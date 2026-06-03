#include "video2vec/query/query_engine.hpp"
#include "video2vec/core/logger.hpp"
#include <algorithm>

namespace video2vec::query {

QueryEngine::QueryEngine(std::shared_ptr<index::IVectorStore> store, std::shared_ptr<embedding::IEmbeddingBackend> embedder)
    : store_(std::move(store)), embedder_(std::move(embedder)) {}

core::Result<std::vector<QueryResult>> QueryEngine::search(const QueryRequest& request) {
    if (!embedder_) return core::Result<std::vector<QueryResult>>(core::Error::from_code(core::ErrorCode::ModelError, "embedding backend not available"));
    if (!store_) return core::Result<std::vector<QueryResult>>(core::Error::from_code(core::ErrorCode::InternalError, "vector store not available"));
    auto emb_result = embedder_->encode_text({request.text});
    if (!emb_result) return core::Result<std::vector<QueryResult>>(emb_result.error());
    auto& embeddings = emb_result.value();
    if (embeddings.empty()) return core::Result<std::vector<QueryResult>>(core::Error::from_code(core::ErrorCode::InternalError, "empty embedding returned"));
    std::vector<float> query_vec;
    if (!embeddings[0].float_data.empty()) query_vec = embeddings[0].float_data;
    else if (!embeddings[0].int8_data.empty()) query_vec = embedding::dequantize_from_int8(embeddings[0].int8_data);
    index::SearchConfig search_cfg{};
    search_cfg.top_k = request.top_k;
    search_cfg.type_filter = request.type_filter;
    search_cfg.temporal_rerank = request.merge_by_time;
    auto search_result = store_->search(query_vec, search_cfg);
    if (!search_result) return core::Result<std::vector<QueryResult>>(search_result.error());
    std::vector<QueryResult> results;
    for (const auto& sr : search_result.value()) {
        QueryResult qr{};
        qr.window_t0_ms = sr.record.ts_ms - request.context_ms_before;
        qr.window_t1_ms = sr.record.ts_ms + request.context_ms_after;
        qr.text = sr.record.text;
        qr.score = sr.score;
        if (!sr.record.proof_uri.empty()) qr.evidence_uris.push_back(sr.record.proof_uri);
        results.push_back(std::move(qr));
    }
    if (request.merge_by_time && results.size() > 1) {
        std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.window_t0_ms < b.window_t0_ms; });
        std::vector<QueryResult> merged;
        merged.push_back(results[0]);
        for (size_t i = 1; i < results.size(); ++i) {
            if (results[i].window_t0_ms - merged.back().window_t1_ms < 10000) {
                merged.back().window_t1_ms = std::max(merged.back().window_t1_ms, results[i].window_t1_ms);
                merged.back().text += " " + results[i].text;
                merged.back().score = std::max(merged.back().score, results[i].score);
                merged.back().evidence_uris.insert(merged.back().evidence_uris.end(), results[i].evidence_uris.begin(), results[i].evidence_uris.end());
            } else {
                merged.push_back(results[i]);
            }
        }
        results = std::move(merged);
    }
    if (request.expand_context) {
        for (auto& r : results) {
            r.window_t0_ms = std::max(int64_t{0}, r.window_t0_ms - request.context_ms_before);
            r.window_t1_ms += request.context_ms_after;
        }
    }
    return core::Result<std::vector<QueryResult>>(std::move(results));
}

core::Result<std::vector<QueryResult>> QueryEngine::search_hybrid(const QueryRequest& request, float text_weight) {
    auto text_results = search(request);
    if (!text_results) return text_results;
    auto& results = text_results.value();
    for (auto& r : results) r.score *= text_weight;
    return core::Result<std::vector<QueryResult>>(std::move(results));
}

core::Result<std::vector<QueryResult>> QueryEngine::search_time_aware(const QueryRequest& request, int64_t target_time_ms) {
    auto results = search(request);
    if (!results) return results;
    auto& qr = results.value();
    for (auto& r : qr) {
        int64_t dt = std::llabs((r.window_t0_ms + r.window_t1_ms) / 2 - target_time_ms);
        float time_penalty = static_cast<float>(dt) / 30000.0f;
        r.score *= std::max(0.1f, 1.0f - time_penalty);
    }
    std::sort(qr.begin(), qr.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    return core::Result<std::vector<QueryResult>>(std::move(qr));
}

} // namespace video2vec::query
