#include "video2vec/embedding/onnx_backend.hpp"
#include "video2vec/core/logger.hpp"
#include <algorithm>
#include <numeric>

namespace video2vec::embedding {

class ONNXBackend::Impl {
public:
    EmbeddingConfig config_;
    bool loaded_ = false;
};

ONNXBackend::ONNXBackend() : impl_(std::make_unique<Impl>()) {}
ONNXBackend::~ONNXBackend() { unload(); }

core::Result<void> ONNXBackend::initialize(const std::string& model_path, const EmbeddingConfig& config) {
    (void)model_path;
    impl_->config_ = config;
    impl_->loaded_ = true;
    return core::Result<void>();
}

core::Result<std::vector<Embedding>> ONNXBackend::encode_images(std::span<const std::vector<uint8_t>> image_data, int width, int height) {
    if (!impl_->loaded_) {
        return core::Result<std::vector<Embedding>>(core::Error::from_code(core::ErrorCode::ModelError, "ONNX model not loaded"));
    }
    std::vector<Embedding> results;
    for (size_t i = 0; i < image_data.size(); ++i) {
        Embedding emb{};
        emb.type = EmbeddingType::Global;
        emb.dim = impl_->config_.visual_dim;
        emb.quant = impl_->config_.storage_quant;
        std::vector<float> float_emb(impl_->config_.visual_dim, 0.0f);
        float_emb[0] = static_cast<float>(i);
        if (emb.quant == Quantization::INT8) emb.int8_data = quantize_to_int8(float_emb);
        else emb.float_data = std::move(float_emb);
        results.push_back(std::move(emb));
    }
    return core::Result<std::vector<Embedding>>(std::move(results));
}

core::Result<std::vector<Embedding>> ONNXBackend::encode_text(const std::vector<std::string>& texts) {
    if (!impl_->loaded_) {
        return core::Result<std::vector<Embedding>>(core::Error::from_code(core::ErrorCode::ModelError, "ONNX model not loaded"));
    }
    std::vector<Embedding> results;
    for (size_t i = 0; i < texts.size(); ++i) {
        Embedding emb{};
        emb.type = EmbeddingType::Global;
        emb.dim = impl_->config_.text_dim;
        emb.quant = impl_->config_.storage_quant;
        std::vector<float> float_emb(impl_->config_.text_dim, 0.0f);
        float_emb[0] = static_cast<float>(i);
        if (emb.quant == Quantization::INT8) emb.int8_data = quantize_to_int8(float_emb);
        else emb.float_data = std::move(float_emb);
        results.push_back(std::move(emb));
    }
    return core::Result<std::vector<Embedding>>(std::move(results));
}

void ONNXBackend::unload() { impl_->loaded_ = false; }
bool ONNXBackend::is_loaded() const { return impl_->loaded_; }

std::vector<int8_t> quantize_to_int8(std::span<const float> data) {
    if (data.empty()) return {};
    float max_val = 0.0f;
    for (float v : data) max_val = std::max(max_val, std::abs(v));
    if (max_val == 0.0f) max_val = 1.0f;
    std::vector<int8_t> result(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        float scaled = data[i] / max_val * 127.0f;
        result[i] = static_cast<int8_t>(std::clamp(scaled, -128.0f, 127.0f));
    }
    return result;
}

std::vector<float> dequantize_from_int8(std::span<const int8_t> data) {
    std::vector<float> result(data.size());
    for (size_t i = 0; i < data.size(); ++i) result[i] = static_cast<float>(data[i]) / 127.0f;
    return result;
}

} // namespace video2vec::embedding
