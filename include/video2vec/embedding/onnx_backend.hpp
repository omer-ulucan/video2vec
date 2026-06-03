#pragma once

#include "video2vec/embedding/embedding_backend.hpp"
#include <memory>

namespace video2vec::embedding {

class ONNXBackend : public IEmbeddingBackend {
public:
    ONNXBackend();
    ~ONNXBackend() override;
    ONNXBackend(const ONNXBackend&) = delete;
    ONNXBackend& operator=(const ONNXBackend&) = delete;
    core::Result<void> initialize(const std::string& model_path, const EmbeddingConfig& config) override;
    core::Result<std::vector<Embedding>> encode_images(std::span<const std::vector<uint8_t>> image_data, int width, int height) override;
    core::Result<std::vector<Embedding>> encode_text(const std::vector<std::string>& texts) override;
    void unload() override;
    [[nodiscard]] bool is_loaded() const override;
    [[nodiscard]] std::string name() const override { return "onnxruntime"; }
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace video2vec::embedding
