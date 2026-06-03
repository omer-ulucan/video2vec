#pragma once

#include "video2vec/core/result.hpp"
#include "video2vec/embedding/embedding_backend.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace video2vec::index {

struct VectorRecord {
    std::string id;
    std::vector<float> vector;
    std::string video_id;
    int window_id = 0;
    int64_t ts_ms = 0;
    std::string type;
    std::string text;
    std::vector<float> bbox;
    double confidence = 0.0;
    int width = 0;
    int height = 0;
    std::string proof_uri;
};

struct SearchResult {
    std::string id;
    float score = 0.0f;
    VectorRecord record;
};

struct SearchConfig {
    int top_k = 8;
    std::string type_filter;
    int64_t time_window_start_ms = 0;
    int64_t time_window_end_ms = 0;
    bool temporal_rerank = true;
};

class IVectorStore {
public:
    virtual ~IVectorStore() = default;
    virtual core::Result<void> initialize(const std::string& path, int dim, const std::string& metric = "cosine") = 0;
    virtual core::Result<void> add(const std::vector<VectorRecord>& records) = 0;
    virtual core::Result<std::vector<SearchResult>> search(std::span<const float> query_vector, const SearchConfig& config) = 0;
    virtual core::Result<void> persist(const std::string& path) = 0;
    virtual core::Result<void> load(const std::string& path) = 0;
    [[nodiscard]] virtual size_t count() const = 0;
    [[nodiscard]] virtual std::string name() const = 0;
};

} // namespace video2vec::index
