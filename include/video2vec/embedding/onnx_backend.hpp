#pragma once

#include "video2vec/embedding/embedding_backend.hpp"
#include <memory>

namespace video2vec::embedding {

// ONNX Runtime embedding backend.
//
// Supported models have exactly one input tensor and at least one output
// tensor; the first output is taken as the embedding.
//  - encode_images(): float [N,3,H,W] input fed with packed RGB scaled to
//    [0, 1]. No model-specific normalization (e.g. CLIP mean/std), resize or
//    center-crop is applied; callers must supply patches at the model's size.
//  - encode_text(): float [N, dim] input. This build ships no tokenizer, so
//    text is encoded as a placeholder character-level feature vector
//    (byte/255 per position). Models with int64 token-id inputs (CLIP,
//    sentence-transformers) are rejected with ErrorCode::Unsupported rather
//    than being fed meaningless data.
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
