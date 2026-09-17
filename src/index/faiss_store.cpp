#include "video2vec/index/faiss_store.hpp"
#include "video2vec/core/logger.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>

#if defined(HAS_FAISS)
#include <faiss/IndexFlat.h>
#endif

namespace video2vec::index {

namespace {
    constexpr int64_t kMaxRecords = 10'000'000;
    constexpr int32_t kMaxStringLen = 1'000'000;
    constexpr int32_t kMaxVectorDim = 65536;
    constexpr char kStoreMagic[4] = {'V', '2', 'V', 'X'};
    constexpr int32_t kStoreVersion = 2;

    // ------------------------------------------------------------ metrics
    enum class Metric { Cosine, InnerProduct, L2 };

    bool parse_metric(const std::string& name, Metric& out) {
        if (name == "cosine") { out = Metric::Cosine; return true; }
        if (name == "inner_product" || name == "ip" || name == "dot") { out = Metric::InnerProduct; return true; }
        if (name == "l2" || name == "euclidean") { out = Metric::L2; return true; }
        return false;
    }

    const char* metric_name(Metric m) {
        switch (m) {
        case Metric::Cosine: return "cosine";
        case Metric::InnerProduct: return "inner_product";
        default: return "l2";
        }
    }

    void l2_normalize(std::vector<float>& v) {
        double norm = 0.0;
        for (float x : v) norm += static_cast<double>(x) * x;
        if (norm <= 0.0) return;
        const float inv = static_cast<float>(1.0 / std::sqrt(norm));
        for (float& x : v) x *= inv;
    }

    // Similarity in "higher is better" form for every metric, so the FAISS
    // and fallback paths rank identically.
    float similarity(Metric metric, std::span<const float> q, std::span<const float> r) {
        double acc = 0.0;
        if (metric == Metric::L2) {
            for (size_t i = 0; i < q.size(); ++i) {
                double d = static_cast<double>(q[i]) - r[i];
                acc += d * d;
            }
            return static_cast<float>(1.0 / (1.0 + acc));  // squared distance -> (0, 1]
        }
        for (size_t i = 0; i < q.size(); ++i) acc += static_cast<double>(q[i]) * r[i];
        return static_cast<float>(acc);
    }

    // ------------------------------------------------------------ binary I/O
    template <typename T>
    void write_pod(std::ostream& out, const T& value) { out.write(reinterpret_cast<const char*>(&value), sizeof(T)); }

    void write_string(std::ostream& out, const std::string& s) {
        write_pod(out, static_cast<int32_t>(s.size()));
        out.write(s.data(), static_cast<std::streamsize>(s.size()));
    }

    void write_floats(std::ostream& out, const std::vector<float>& v) {
        write_pod(out, static_cast<int32_t>(v.size()));
        out.write(reinterpret_cast<const char*>(v.data()), static_cast<std::streamsize>(v.size() * sizeof(float)));
    }

    template <typename T>
    bool read_exact(std::istream& stream, T& value) {
        stream.read(reinterpret_cast<char*>(&value), sizeof(T));
        return stream.good() && static_cast<size_t>(stream.gcount()) == sizeof(T);
    }

    bool read_string(std::istream& stream, std::string& out, int32_t max_len) {
        int32_t len = 0;
        if (!read_exact(stream, len)) return false;
        if (len < 0 || len > max_len) return false;
        out.resize(static_cast<size_t>(len));
        if (len > 0) {
            stream.read(out.data(), len);
            if (!stream.good() || static_cast<size_t>(stream.gcount()) != static_cast<size_t>(len)) return false;
        }
        return true;
    }

    bool read_floats(std::istream& stream, std::vector<float>& out, int32_t max_len) {
        int32_t len = 0;
        if (!read_exact(stream, len)) return false;
        if (len < 0 || len > max_len) return false;
        out.resize(static_cast<size_t>(len));
        if (len > 0) {
            stream.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(len) * sizeof(float));
            if (!stream.good() || static_cast<size_t>(stream.gcount()) != static_cast<size_t>(len) * sizeof(float)) return false;
        }
        return true;
    }

    core::Result<void> io_error(const std::string& what) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::IoError, what));
    }
    core::Result<void> invalid(const std::string& what) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::InvalidArgument, what));
    }
}

class FAISSStore::Impl {
public:
    int dim_ = 0;
    Metric metric_ = Metric::Cosine;
    std::vector<VectorRecord> records_;  // vectors are stored L2-normalized for the cosine metric
    bool initialized_ = false;
#if defined(HAS_FAISS)
    std::unique_ptr<faiss::Index> index_;

    void rebuild_index() {
        if (metric_ == Metric::L2) index_ = std::make_unique<faiss::IndexFlatL2>(dim_);
        else index_ = std::make_unique<faiss::IndexFlatIP>(dim_);
        if (records_.empty()) return;
        std::vector<float> flat;
        flat.reserve(records_.size() * static_cast<size_t>(dim_));
        for (const auto& r : records_) flat.insert(flat.end(), r.vector.begin(), r.vector.end());
        index_->add(static_cast<faiss::idx_t>(records_.size()), flat.data());
    }
#endif

    bool matches(const VectorRecord& record, const SearchConfig& config) const {
        if (!config.type_filter.empty() && record.type != config.type_filter) return false;
        if (config.time_window_end_ms > config.time_window_start_ms) {
            if (record.ts_ms < config.time_window_start_ms || record.ts_ms > config.time_window_end_ms) return false;
        }
        return true;
    }
};

FAISSStore::FAISSStore() : impl_(std::make_unique<Impl>()) {}
FAISSStore::~FAISSStore() = default;

core::Result<void> FAISSStore::initialize(const std::string& path, int dim, const std::string& metric) {
    (void)path;  // persist()/load() take the location explicitly; initialize() never auto-loads
    if (dim <= 0 || dim > kMaxVectorDim) return invalid("vector dimension must be in 1.." + std::to_string(kMaxVectorDim));
    Metric m;
    if (!parse_metric(metric, m)) return invalid("unknown metric '" + metric + "' (expected cosine, inner_product or l2)");
    impl_->dim_ = dim;
    impl_->metric_ = m;
    impl_->records_.clear();
    impl_->initialized_ = true;
#if defined(HAS_FAISS)
    impl_->rebuild_index();
#else
    core::Logger::warn("FAISS not available, using in-memory linear search", {});
#endif
    return core::Result<void>();
}

core::Result<void> FAISSStore::add(const std::vector<VectorRecord>& records) {
    if (!impl_->initialized_) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::InternalError, "store not initialized"));
    }
    for (size_t i = 0; i < records.size(); ++i) {
        if (records[i].vector.size() != static_cast<size_t>(impl_->dim_)) {
            return invalid("record " + std::to_string(i) + " ('" + records[i].id + "') has dimension " +
                           std::to_string(records[i].vector.size()) + ", store expects " + std::to_string(impl_->dim_));
        }
    }
    std::vector<VectorRecord> prepared = records;
    if (impl_->metric_ == Metric::Cosine) {
        for (auto& r : prepared) l2_normalize(r.vector);
    }
#if defined(HAS_FAISS)
    if (!impl_->index_) return core::Result<void>(core::Error::from_code(core::ErrorCode::InternalError, "FAISS index not created"));
    if (!prepared.empty()) {
        std::vector<float> flat;
        flat.reserve(prepared.size() * static_cast<size_t>(impl_->dim_));
        for (const auto& r : prepared) flat.insert(flat.end(), r.vector.begin(), r.vector.end());
        // Add to the index before records_ so the two can never diverge.
        impl_->index_->add(static_cast<faiss::idx_t>(prepared.size()), flat.data());
    }
#endif
    impl_->records_.insert(impl_->records_.end(),
                           std::make_move_iterator(prepared.begin()), std::make_move_iterator(prepared.end()));
    return core::Result<void>();
}

core::Result<std::vector<SearchResult>> FAISSStore::search(std::span<const float> query_vector, const SearchConfig& config) {
    using R = core::Result<std::vector<SearchResult>>;
    if (!impl_->initialized_) {
        return R(core::Error::from_code(core::ErrorCode::InternalError, "store not initialized"));
    }
    if (query_vector.size() != static_cast<size_t>(impl_->dim_)) {
        return R(core::Error::from_code(core::ErrorCode::InvalidArgument, "query vector dimension mismatch"));
    }
    if (config.top_k <= 0) {
        return R(core::Error::from_code(core::ErrorCode::InvalidArgument, "top_k must be positive"));
    }
    std::vector<float> query(query_vector.begin(), query_vector.end());
    if (impl_->metric_ == Metric::Cosine) l2_normalize(query);

    std::vector<SearchResult> results;
    const size_t top_k = static_cast<size_t>(config.top_k);
    const bool filtered = !config.type_filter.empty() || config.time_window_end_ms > config.time_window_start_ms;

#if defined(HAS_FAISS)
    if (impl_->index_ && impl_->index_->ntotal > 0) {
        // Over-fetch when filtering so post-filtering can still fill top_k.
        size_t k = std::min<size_t>(static_cast<size_t>(impl_->index_->ntotal), filtered ? top_k * 8 : top_k);
        std::vector<faiss::idx_t> indices(k);
        std::vector<float> distances(k);
        impl_->index_->search(1, query.data(), static_cast<faiss::idx_t>(k), distances.data(), indices.data());
        for (size_t i = 0; i < k && results.size() < top_k; ++i) {
            if (indices[i] < 0 || indices[i] >= static_cast<faiss::idx_t>(impl_->records_.size())) continue;
            const auto& record = impl_->records_[static_cast<size_t>(indices[i])];
            if (!impl_->matches(record, config)) continue;
            SearchResult sr{};
            sr.id = record.id;
            sr.score = impl_->metric_ == Metric::L2 ? static_cast<float>(1.0 / (1.0 + distances[i])) : distances[i];
            sr.record = record;
            results.push_back(std::move(sr));
        }
    }
#else
    (void)filtered;
    std::vector<std::pair<float, size_t>> scored;
    scored.reserve(impl_->records_.size());
    for (size_t i = 0; i < impl_->records_.size(); ++i) {
        const auto& record = impl_->records_[i];
        if (!impl_->matches(record, config)) continue;
        scored.push_back({similarity(impl_->metric_, query, record.vector), i});
    }
    const size_t keep = std::min(top_k, scored.size());
    std::partial_sort(scored.begin(), scored.begin() + static_cast<std::ptrdiff_t>(keep), scored.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < keep; ++i) {
        SearchResult sr{};
        sr.id = impl_->records_[scored[i].second].id;
        sr.score = scored[i].first;
        sr.record = impl_->records_[scored[i].second];
        results.push_back(std::move(sr));
    }
#endif

    if (config.temporal_rerank && results.size() > 1) {
        // One bounded boost per result (at most 1.5x) from its closest
        // temporal neighbour. The previous per-pair multiplier compounded,
        // letting dense clusters inflate scores without limit.
        std::vector<float> boosts(results.size(), 1.0f);
        for (size_t i = 0; i < results.size(); ++i) {
            for (size_t j = 0; j < results.size(); ++j) {
                if (i == j) continue;
                const int64_t a = results[i].record.ts_ms, b = results[j].record.ts_ms;
                const int64_t dt = a > b ? a - b : b - a;
                if (dt < 5000) boosts[i] = std::max(boosts[i], 1.0f + 0.5f * (5000.0f - static_cast<float>(dt)) / 5000.0f);
            }
        }
        for (size_t i = 0; i < results.size(); ++i) results[i].score *= boosts[i];
    }
    std::stable_sort(results.begin(), results.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    return R(std::move(results));
}

core::Result<void> FAISSStore::persist(const std::string& path) {
    if (!impl_->initialized_) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::InternalError, "store not initialized"));
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) return io_error("cannot open file for writing: " + path);
    file.write(kStoreMagic, 4);
    write_pod(file, kStoreVersion);
    write_pod(file, static_cast<int32_t>(impl_->dim_));
    write_string(file, metric_name(impl_->metric_));
    write_pod(file, static_cast<int64_t>(impl_->records_.size()));
    for (const auto& r : impl_->records_) {
        write_string(file, r.id);
        write_floats(file, r.vector);
        write_string(file, r.video_id);
        write_pod(file, static_cast<int32_t>(r.window_id));
        write_pod(file, static_cast<int64_t>(r.ts_ms));
        write_string(file, r.type);
        write_string(file, r.text);
        write_floats(file, r.bbox);
        write_pod(file, r.confidence);
        write_pod(file, static_cast<int32_t>(r.width));
        write_pod(file, static_cast<int32_t>(r.height));
        write_string(file, r.proof_uri);
    }
    file.close();
    if (!file.good()) return io_error("incomplete write during persist: " + path);
    return core::Result<void>();
}

core::Result<void> FAISSStore::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return io_error("cannot open file for reading: " + path);

    char magic[4] = {0, 0, 0, 0};
    file.read(magic, 4);
    if (!file.good() || std::memcmp(magic, kStoreMagic, 4) != 0) {
        return invalid("not a video2vec index file (bad magic): " + path);
    }
    int32_t version = 0;
    if (!read_exact(file, version)) return io_error("failed to read index version");
    if (version != kStoreVersion) {
        return invalid("unsupported index file version " + std::to_string(version) + " (expected " + std::to_string(kStoreVersion) + ")");
    }
    int32_t dim = 0;
    if (!read_exact(file, dim)) return io_error("failed to read dimension");
    if (dim <= 0 || dim > kMaxVectorDim) return invalid("invalid dimension in store file");
    std::string metric;
    if (!read_string(file, metric, 64)) return io_error("failed to read metric");
    Metric m;
    if (!parse_metric(metric, m)) return invalid("unknown metric in store file: " + metric);
    int64_t count = 0;
    if (!read_exact(file, count)) return io_error("failed to read record count");
    if (count < 0 || count > kMaxRecords) return invalid("invalid record count in store file");

    std::vector<VectorRecord> records;
    for (int64_t i = 0; i < count; ++i) {
        VectorRecord r{};
        const std::string where = " (record " + std::to_string(i) + ")";
        int32_t window_id = 0, width = 0, height = 0;
        int64_t ts = 0;
        if (!read_string(file, r.id, kMaxStringLen)) return io_error("failed to read record id" + where);
        if (!read_floats(file, r.vector, kMaxVectorDim)) return io_error("failed to read vector" + where);
        if (r.vector.size() != static_cast<size_t>(dim)) return invalid("vector dimension mismatch" + where);
        if (!read_string(file, r.video_id, kMaxStringLen)) return io_error("failed to read video id" + where);
        if (!read_exact(file, window_id)) return io_error("failed to read window id" + where);
        if (!read_exact(file, ts)) return io_error("failed to read timestamp" + where);
        if (!read_string(file, r.type, kMaxStringLen)) return io_error("failed to read record type" + where);
        if (!read_string(file, r.text, kMaxStringLen)) return io_error("failed to read record text" + where);
        if (!read_floats(file, r.bbox, 16)) return io_error("failed to read bbox" + where);
        if (!read_exact(file, r.confidence)) return io_error("failed to read confidence" + where);
        if (!read_exact(file, width)) return io_error("failed to read width" + where);
        if (!read_exact(file, height)) return io_error("failed to read height" + where);
        if (!read_string(file, r.proof_uri, kMaxStringLen)) return io_error("failed to read proof uri" + where);
        r.window_id = window_id;
        r.ts_ms = ts;
        r.width = width;
        r.height = height;
        records.push_back(std::move(r));
    }

    impl_->dim_ = dim;
    impl_->metric_ = m;
    impl_->records_ = std::move(records);
    impl_->initialized_ = true;
#if defined(HAS_FAISS)
    impl_->rebuild_index();  // load() never rebuilt the index before, so every search after it returned nothing
#endif
    return core::Result<void>();
}

size_t FAISSStore::count() const { return impl_->records_.size(); }

} // namespace video2vec::index
