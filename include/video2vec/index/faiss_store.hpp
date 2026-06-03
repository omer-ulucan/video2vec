#pragma once

#include "video2vec/index/vector_store.hpp"
#include <memory>

namespace video2vec::index {

class FAISSStore : public IVectorStore {
public:
    FAISSStore();
    ~FAISSStore() override;
    FAISSStore(const FAISSStore&) = delete;
    FAISSStore& operator=(const FAISSStore&) = delete;
    core::Result<void> initialize(const std::string& path, int dim, const std::string& metric = "cosine") override;
    core::Result<void> add(const std::vector<VectorRecord>& records) override;
    core::Result<std::vector<SearchResult>> search(std::span<const float> query_vector, const SearchConfig& config) override;
    core::Result<void> persist(const std::string& path) override;
    core::Result<void> load(const std::string& path) override;
    [[nodiscard]] size_t count() const override;
    [[nodiscard]] std::string name() const override { return "faiss"; }
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace video2vec::index
