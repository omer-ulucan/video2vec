#include "video2vec/query/query_engine.hpp"
#include "video2vec/core/logger.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace video2vec::query {

namespace {
    // Lower-cased alphanumeric tokens of `text`.
    std::set<std::string> tokenize(const std::string& text) {
        std::set<std::string> tokens;
        std::string current;
        for (unsigned char c : text) {
            if (std::isalnum(c)) {
                current.push_back(static_cast<char>(std::tolower(c)));
            } else if (!current.empty()) {
                tokens.insert(current);
                current.clear();
            }
        }
        if (!current.empty()) tokens.insert(current);
        return tokens;
    }

    // Fraction of the query's tokens that appear in `text`, in [0, 1].
    float keyword_overlap(const std::set<std::string>& query_tokens, const std::string& text) {
        if (query_tokens.empty()) return 0.0f;
        const auto text_tokens = tokenize(text);
        size_t hits = 0;
        for (const auto& t : query_tokens) hits += text_tokens.count(t);
        return static_cast<float>(hits) / static_cast<float>(query_tokens.size());
    }
}

QueryEngine::QueryEngine(std::shared_ptr<index::IVectorStore> store, std::shared_ptr<embedding::IEmbeddingBackend> embedder)
    : store_(std::move(store)), embedder_(std::move(embedder)) {}

core::Result<std::vector<QueryResult>> QueryEngine::search(const QueryRequest& request) {
    using R = core::Result<std::vector<QueryResult>>;
    if (!embedder_) return R(core::Error::from_code(core::ErrorCode::ModelError, "embedding backend not available"));
    if (!store_) return R(core::Error::from_code(core::ErrorCode::InternalError, "vector store not available"));
    if (request.top_k <= 0) return R(core::Error::from_code(core::ErrorCode::InvalidArgument, "top_k must be positive"));
    auto emb_result = embedder_->encode_text({request.text});
    if (!emb_result) return R(emb_result.error());
    auto& embeddings = emb_result.value();
    if (embeddings.empty()) return R(core::Error::from_code(core::ErrorCode::InternalError, "empty embedding returned"));
    std::vector<float> query_vec = embedding::to_float(embeddings[0]);  // honours the int8 scale

    index::SearchConfig search_cfg{};
    search_cfg.top_k = request.top_k;
    search_cfg.type_filter = request.type_filter;
    search_cfg.temporal_rerank = request.merge_by_time;
    auto search_result = store_->search(query_vec, search_cfg);
    if (!search_result) return R(search_result.error());

    std::vector<QueryResult> results;
    for (const auto& sr : search_result.value()) {
        QueryResult qr{};
        qr.window_t0_ms = std::max<int64_t>(0, sr.record.ts_ms - request.context_ms_before);
        qr.window_t1_ms = sr.record.ts_ms + request.context_ms_after;
        qr.text = sr.record.text;
        qr.score = sr.score;
        if (!sr.record.proof_uri.empty()) qr.evidence_uris.push_back(sr.record.proof_uri);
        results.push_back(std::move(qr));
    }

    if (request.merge_by_time && results.size() > 1) {
        // Merge hits whose context windows overlap or nearly touch into one
        // result that keeps the best score, unique text and unique evidence.
        std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.window_t0_ms < b.window_t0_ms; });
        const int64_t merge_gap_ms = request.context_ms_before + request.context_ms_after;
        std::vector<QueryResult> merged;
        merged.push_back(results[0]);
        for (size_t i = 1; i < results.size(); ++i) {
            auto& last = merged.back();
            if (results[i].window_t0_ms - last.window_t1_ms < merge_gap_ms) {
                last.window_t1_ms = std::max(last.window_t1_ms, results[i].window_t1_ms);
                if (!results[i].text.empty() && last.text.find(results[i].text) == std::string::npos) {
                    if (!last.text.empty()) last.text += " ";
                    last.text += results[i].text;
                }
                last.score = std::max(last.score, results[i].score);
                for (const auto& uri : results[i].evidence_uris) {
                    if (std::find(last.evidence_uris.begin(), last.evidence_uris.end(), uri) == last.evidence_uris.end()) {
                        last.evidence_uris.push_back(uri);
                    }
                }
            } else {
                merged.push_back(results[i]);
            }
        }
        results = std::move(merged);
    }
    if (request.expand_context) {
        for (auto& r : results) {
            r.window_t0_ms = std::max<int64_t>(0, r.window_t0_ms - request.context_ms_before);
            r.window_t1_ms += request.context_ms_after;
        }
    }
    // Merging sorted by time; callers expect the best match first.
    std::stable_sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    return R(std::move(results));
}

core::Result<std::vector<QueryResult>> QueryEngine::search_hybrid(const QueryRequest& request, float text_weight) {
    auto vector_results = search(request);
    if (!vector_results) return vector_results;
    auto& results = vector_results.value();
    // Blend the vector score with lexical overlap between the query and the
    // result text. Previously every score was multiplied by the same
    // constant, which could not change the ranking.
    const float w = std::clamp(text_weight, 0.0f, 1.0f);
    const auto query_tokens = tokenize(request.text);
    for (auto& r : results) r.score = w * r.score + (1.0f - w) * keyword_overlap(query_tokens, r.text);
    std::stable_sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
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
    std::stable_sort(qr.begin(), qr.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    return core::Result<std::vector<QueryResult>>(std::move(qr));
}

} // namespace video2vec::query
