#pragma once

#include "video2vec/core/result.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace video2vec::embedding {

enum class EmbeddingType { Global = 0, Patch = 1 };
enum class Quantization { FP32 = 0, FP16 = 1, INT8 = 2 };

struct Embedding {
    EmbeddingType type = EmbeddingType::Global;
    Quantization quant = Quantization::INT8;
    int dim = 0;
    std::vector<int8_t> int8_data;
    std::vector<float> float_data;
};

struct EmbeddingConfig {
    int visual_dim = 512;
    int text_dim = 384;
    Quantization storage_quant = Quantization::INT8;
    bool use_fp16_inference = true;
    int batch_size = 8;
};

class IEmbeddingBackend {
public:
    virtual ~IEmbeddingBackend() = default;
    virtual core::Result<void> initialize(const std::string& model_path, const EmbeddingConfig& config) = 0;
    virtual core::Result<std::vector<Embedding>> encode_images(std::span<const std::vector<uint8_t>> image_data, int width, int height) = 0;
    virtual core::Result<std::vector<Embedding>> encode_text(const std::vector<std::string>& texts) = 0;
    virtual void unload() = 0;
    [[nodiscard]] virtual bool is_loaded() const = 0;
    [[nodiscard]] virtual std::string name() const = 0;
};

std::vector<int8_t> quantize_to_int8(std::span<const float> data);
std::vector<float> dequantize_from_int8(std::span<const int8_t> data);

} // namespace video2vec::embedding
