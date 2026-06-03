#include "video2vec/embedding/onnx_backend.hpp"
#include "video2vec/core/logger.hpp"
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <numeric>

namespace video2vec::embedding {

class ONNXBackend::Impl {
public:
    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "video2vec"};
    std::unique_ptr<Ort::Session> session_;
    std::unique_ptr<Ort::SessionOptions> session_options_;
    EmbeddingConfig config_;
    bool loaded_ = false;
    std::vector<float> input_buffer_;
    std::vector<float> output_buffer_;
};

ONNXBackend::ONNXBackend() : impl_(std::make_unique<Impl>()) {}
ONNXBackend::~ONNXBackend() { unload(); }

core::Result<void> ONNXBackend::initialize(const std::string& model_path, const EmbeddingConfig& config) {
    unload();
    try {
        impl_->session_options_ = std::make_unique<Ort::SessionOptions>();
        impl_->session_options_->SetIntraOpNumThreads(4);
        impl_->session_options_->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        impl_->session_ = std::make_unique<Ort::Session>(impl_->env_, model_path.c_str(), *impl_->session_options_);
        impl_->config_ = config;
        impl_->loaded_ = true;
    } catch (const Ort::Exception& e) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::ModelError,
            std::string("ONNX Runtime initialization failed: ") + e.what()));
    }
    return core::Result<void>();
}

core::Result<std::vector<Embedding>> ONNXBackend::encode_images(std::span<const std::vector<uint8_t>> image_data, int width, int height) {
    if (!impl_->loaded_ || !impl_->session_) {
        return core::Result<std::vector<Embedding>>(core::Error::from_code(core::ErrorCode::ModelError, "ONNX model not loaded"));
    }
    try {
        Ort::AllocatorWithDefaultOptions allocator;
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<Embedding> results;
        size_t batch_size = std::min(image_data.size(), static_cast<size_t>(impl_->config_.batch_size));
        for (size_t batch_start = 0; batch_start < image_data.size(); batch_start += batch_size) {
            size_t current_batch = std::min(batch_size, image_data.size() - batch_start);
            std::vector<float> input_data;
            input_data.reserve(current_batch * 3 * width * height);
            for (size_t i = batch_start; i < batch_start + current_batch; ++i) {
                const auto& img = image_data[i];
                if (img.size() < static_cast<size_t>(width * height * 3)) continue;
                for (int c = 0; c < 3; ++c) {
                    for (int y = 0; y < height; ++y) {
                        for (int x = 0; x < width; ++x) {
                            size_t idx = (y * width + x) * 3 + c;
                            input_data.push_back(img[idx] / 255.0f);
                        }
                    }
                }
            }
            if (input_data.empty()) continue;
            std::vector<int64_t> input_shape = {static_cast<int64_t>(current_batch), 3, height, width};
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(memory_info, input_data.data(), input_data.size(), input_shape.data(), input_shape.size());
            const char* input_names[] = {"input"};
            const char* output_names[] = {"output"};
            auto output_tensors = impl_->session_->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1, output_names, 1);
            float* output_data = output_tensors[0].GetTensorMutableData<float>();
            auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
            int64_t output_count = 1;
            for (auto dim : output_shape) output_count *= dim;
            int dim = impl_->config_.visual_dim;
            for (size_t i = 0; i < current_batch; ++i) {
                Embedding emb{};
                emb.type = EmbeddingType::Global;
                emb.dim = dim;
                emb.quant = impl_->config_.storage_quant;
                size_t offset = i * dim;
                std::vector<float> float_emb(output_data + offset, output_data + offset + dim);
                if (emb.quant == Quantization::INT8) emb.int8_data = quantize_to_int8(float_emb);
                else emb.float_data = std::move(float_emb);
                results.push_back(std::move(emb));
            }
        }
        return core::Result<std::vector<Embedding>>(std::move(results));
    } catch (const Ort::Exception& e) {
        return core::Result<std::vector<Embedding>>(core::Error::from_code(core::ErrorCode::ModelError,
            std::string("ONNX inference failed: ") + e.what()));
    }
}

core::Result<std::vector<Embedding>> ONNXBackend::encode_text(const std::vector<std::string>& texts) {
    if (!impl_->loaded_ || !impl_->session_) {
        return core::Result<std::vector<Embedding>>(core::Error::from_code(core::ErrorCode::ModelError, "ONNX model not loaded"));
    }
    std::vector<Embedding> results;
    for (size_t i = 0; i < texts.size(); ++i) {
        Embedding emb{};
        emb.type = EmbeddingType::Global;
        emb.dim = impl_->config_.text_dim;
        emb.quant = impl_->config_.storage_quant;
        std::vector<float> float_emb(impl_->config_.text_dim);
        size_t hash = 0;
        for (char c : texts[i]) hash = hash * 31 + static_cast<unsigned char>(c);
        for (int j = 0; j < impl_->config_.text_dim; ++j) float_emb[j] = static_cast<float>(std::sin(hash + j));
        double norm = 0.0;
        for (float v : float_emb) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 0) for (auto& v : float_emb) v /= static_cast<float>(norm);
        if (emb.quant == Quantization::INT8) emb.int8_data = quantize_to_int8(float_emb);
        else emb.float_data = std::move(float_emb);
        results.push_back(std::move(emb));
    }
    return core::Result<std::vector<Embedding>>(std::move(results));
}

void ONNXBackend::unload() {
    impl_->session_.reset();
    impl_->session_options_.reset();
    impl_->loaded_ = false;
}

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
