#include "video2vec/index/faiss_store.hpp"
#include "video2vec/core/logger.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <numeric>

#if defined(HAS_FAISS)
#include <faiss/IndexFlat.h>
#include <faiss/IndexIVFPQ.h>
#include <faiss/index_io.h>
#endif

namespace video2vec::index {

namespace {
    constexpr int64_t kMaxRecords = 10'000'000;
    constexpr int32_t kMaxStringLen = 1'000'000;
    constexpr int32_t kMaxVectorDim = 65536;

    template <typename T>
    bool read_exact(std::istream& stream, T& value) {
        stream.read(reinterpret_cast<char*>(&value), sizeof(T));
        return stream.good() && static_cast<size_t>(stream.gcount()) == sizeof(T);
    }

    bool read_string(std::istream& stream, std::string& out, int32_t max_len) {
        int32_t len = 0;
        if (!read_exact(stream, len)) return false;
        if (len < 0 || len > max_len) return false;
        out.resize(len);
        if (len > 0) {
            stream.read(out.data(), len);
            if (!stream.good() || static_cast<size_t>(stream.gcount()) != static_cast<size_t>(len)) return false;
        }
        return true;
    }
}

class FAISSStore::Impl {
public:
    int dim_ = 0;
    std::string metric_ = "cosine";
    std::vector<VectorRecord> records_;
    bool initialized_ = false;
#if defined(HAS_FAISS)
    std::unique_ptr<faiss::Index> index_;
#endif
};

FAISSStore::FAISSStore() : impl_(std::make_unique<Impl>()) {}
FAISSStore::~FAISSStore() = default;

core::Result<void> FAISSStore::initialize(const std::string& path, int dim, const std::string& metric) {
    (void)path;
    impl_->dim_ = dim;
    impl_->metric_ = metric;
    impl_->initialized_ = true;
#if defined(HAS_FAISS)
    if (metric == "cosine" || metric == "inner_product") impl_->index_ = std::make_unique<faiss::IndexFlatIP>(dim);
    else impl_->index_ = std::make_unique<faiss::IndexFlatL2>(dim);
#else
    core::Logger::warn("FAISS not available, using in-memory linear search", {});
#endif
    return core::Result<void>();
}

core::Result<void> FAISSStore::add(const std::vector<VectorRecord>& records) {
    if (!impl_->initialized_) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::InternalError, "store not initialized"));
    }
    size_t start_id = impl_->records_.size();
    impl_->records_.insert(impl_->records_.end(), records.begin(), records.end());
#if defined(HAS_FAISS)
    if (!impl_->index_) return core::Result<void>(core::Error::from_code(core::ErrorCode::InternalError, "FAISS index not created"));
    std::vector<float> vectors;
    vectors.reserve(records.size() * impl_->dim_);
    for (const auto& r : records) {
        for (size_t i = 0; i < static_cast<size_t>(impl_->dim_); ++i) {
            if (i < r.vector.size()) vectors.push_back(r.vector[i]);
            else vectors.push_back(0.0f);
        }
    }
    if (!vectors.empty()) impl_->index_->add(static_cast<faiss::idx_t>(records.size()), vectors.data());
#endif
    return core::Result<void>();
}

core::Result<std::vector<SearchResult>> FAISSStore::search(std::span<const float> query_vector, const SearchConfig& config) {
    if (!impl_->initialized_) {
        return core::Result<std::vector<SearchResult>>(core::Error::from_code(core::ErrorCode::InternalError, "store not initialized"));
    }
    if (query_vector.size() != static_cast<size_t>(impl_->dim_)) {
        return core::Result<std::vector<SearchResult>>(core::Error::from_code(core::ErrorCode::InvalidArgument, "query vector dimension mismatch"));
    }
    std::vector<SearchResult> results;
#if defined(HAS_FAISS)
    if (impl_->index_) {
        std::vector<faiss::idx_t> indices(config.top_k);
        std::vector<float> distances(config.top_k);
        impl_->index_->search(1, query_vector.data(), config.top_k, distances.data(), indices.data());
        for (int i = 0; i < config.top_k; ++i) {
            if (indices[i] < 0 || indices[i] >= static_cast<faiss::idx_t>(impl_->records_.size())) continue;
            const auto& record = impl_->records_[indices[i]];
            if (!config.type_filter.empty() && record.type != config.type_filter) continue;
            if (config.time_window_end_ms > config.time_window_start_ms) {
                if (record.ts_ms < config.time_window_start_ms || record.ts_ms > config.time_window_end_ms) continue;
            }
            SearchResult sr{};
            sr.id = record.id;
            sr.score = distances[i];
            sr.record = record;
            results.push_back(std::move(sr));
        }
    }
#else
    std::vector<std::pair<float, size_t>> scored;
    scored.reserve(impl_->records_.size());
    for (size_t i = 0; i < impl_->records_.size(); ++i) {
        const auto& record = impl_->records_[i];
        if (!config.type_filter.empty() && record.type != config.type_filter) continue;
        if (config.time_window_end_ms > config.time_window_start_ms) {
            if (record.ts_ms < config.time_window_start_ms || record.ts_ms > config.time_window_end_ms) continue;
        }
        float score = 0.0f;
        if (record.vector.size() == query_vector.size()) {
            double dot = 0.0, norm_q = 0.0, norm_r = 0.0;
            for (size_t j = 0; j < query_vector.size(); ++j) {
                dot += query_vector[j] * record.vector[j];
                norm_q += query_vector[j] * query_vector[j];
                norm_r += record.vector[j] * record.vector[j];
            }
            double denom = std::sqrt(norm_q) * std::sqrt(norm_r);
            if (denom > 0) score = static_cast<float>(dot / denom);
        }
        scored.push_back({score, i});
    }
    std::partial_sort(scored.begin(), scored.begin() + std::min(static_cast<size_t>(config.top_k), scored.size()), scored.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < std::min(static_cast<size_t>(config.top_k), scored.size()); ++i) {
        SearchResult sr{};
        sr.id = impl_->records_[scored[i].second].id;
        sr.score = scored[i].first;
        sr.record = impl_->records_[scored[i].second];
        results.push_back(std::move(sr));
    }
#endif
    if (config.temporal_rerank && results.size() > 1) {
        for (size_t i = 0; i < results.size(); ++i) {
            for (size_t j = i + 1; j < results.size(); ++j) {
                int64_t dt = std::abs(results[i].record.ts_ms - results[j].record.ts_ms);
                if (dt < 5000) {
                    float boost = 1.0f + (5000.0f - static_cast<float>(dt)) / 10000.0f;
                    results[i].score *= boost;
                    results[j].score *= boost;
                }
            }
        }
        std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    }
    return core::Result<std::vector<SearchResult>>(std::move(results));
}

core::Result<void> FAISSStore::persist(const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "cannot open file for writing: " + path));
    int32_t dim = impl_->dim_;
    file.write(reinterpret_cast<const char*>(&dim), sizeof(dim));
    int32_t metric_len = static_cast<int32_t>(impl_->metric_.size());
    file.write(reinterpret_cast<const char*>(&metric_len), sizeof(metric_len));
    file.write(impl_->metric_.data(), impl_->metric_.size());
    int64_t count = static_cast<int64_t>(impl_->records_.size());
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const auto& r : impl_->records_) {
        int32_t id_len = static_cast<int32_t>(r.id.size());
        file.write(reinterpret_cast<const char*>(&id_len), sizeof(id_len));
        file.write(r.id.data(), r.id.size());
        int32_t vec_len = static_cast<int32_t>(r.vector.size());
        file.write(reinterpret_cast<const char*>(&vec_len), sizeof(vec_len));
        file.write(reinterpret_cast<const char*>(r.vector.data()), vec_len * sizeof(float));
        int64_t ts = r.ts_ms;
        file.write(reinterpret_cast<const char*>(&ts), sizeof(ts));
        int32_t type_len = static_cast<int32_t>(r.type.size());
        file.write(reinterpret_cast<const char*>(&type_len), sizeof(type_len));
        file.write(r.type.data(), r.type.size());
        int32_t text_len = static_cast<int32_t>(r.text.size());
        file.write(reinterpret_cast<const char*>(&text_len), sizeof(text_len));
        file.write(r.text.data(), r.text.size());
    }
    if (!file.good()) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "incomplete write during persist: " + path));
    }
    return core::Result<void>();
}

core::Result<void> FAISSStore::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "cannot open file for reading: " + path));

    int32_t dim = 0;
    if (!read_exact(file, dim)) return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "failed to read dimension"));
    if (dim < 0 || dim > kMaxVectorDim) return core::Result<void>(core::Error::from_code(core::ErrorCode::InvalidArgument, "invalid dimension in store file"));
    impl_->dim_ = dim;

    if (!read_string(file, impl_->metric_, kMaxStringLen)) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "failed to read metric"));
    }

    int64_t count = 0;
    if (!read_exact(file, count)) return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "failed to read record count"));
    if (count < 0 || count > kMaxRecords) return core::Result<void>(core::Error::from_code(core::ErrorCode::InvalidArgument, "invalid record count in store file"));

    impl_->records_.clear();
    impl_->records_.reserve(static_cast<size_t>(count));
    for (int64_t i = 0; i < count; ++i) {
        VectorRecord r{};
        if (!read_string(file, r.id, kMaxStringLen)) {
            return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "failed to read record id at index " + std::to_string(i)));
        }
        int32_t vec_len = 0;
        if (!read_exact(file, vec_len)) return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "failed to read vector length"));
        if (vec_len < 0 || vec_len > kMaxVectorDim) return core::Result<void>(core::Error::from_code(core::ErrorCode::InvalidArgument, "invalid vector length"));
        r.vector.resize(vec_len);
        if (vec_len > 0) {
            file.read(reinterpret_cast<char*>(r.vector.data()), vec_len * sizeof(float));
            if (!file.good() || static_cast<size_t>(file.gcount()) != static_cast<size_t>(vec_len) * sizeof(float)) {
                return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "failed to read vector data"));
            }
        }
        int64_t ts = 0;
        if (!read_exact(file, ts)) return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "failed to read timestamp"));
        r.ts_ms = ts;
        if (!read_string(file, r.type, kMaxStringLen)) {
            return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "failed to read record type"));
        }
        if (!read_string(file, r.text, kMaxStringLen)) {
            return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, "failed to read record text"));
        }
        impl_->records_.push_back(std::move(r));
    }
    impl_->initialized_ = true;
    return core::Result<void>();
}

size_t FAISSStore::count() const { return impl_->records_.size(); }

} // namespace video2vec::index
