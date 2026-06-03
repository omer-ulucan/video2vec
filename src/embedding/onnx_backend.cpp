#include "video2vec/embedding/onnx_backend.hpp"
#include "video2vec/core/logger.hpp"
#include <algorithm>
#include <numeric>

#include "onnxruntime_cxx_api.h"

namespace video2vec::embedding {

class ONNXBackend::Impl {
public:
    EmbeddingConfig config_;
    bool loaded_ = false;
    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "video2vec"};
    std::unique_ptr<Ort::Session> session_;
    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;

    std::vector<const char*> input_name_ptrs() const {
        std::vector<const char*> ptrs;
        for (const auto& s : input_names_) ptrs.push_back(s.c_str());
        return ptrs;
    }
    std::vector<const char*> output_name_ptrs() const {
        std::vector<const char*> ptrs;
        for (const auto& s : output_names_) ptrs.push_back(s.c_str());
        return ptrs;
    }
};

ONNXBackend::ONNXBackend() : impl_(std::make_unique<Impl>()) {}
ONNXBackend::~ONNXBackend() { unload(); }

core::Result<void> ONNXBackend::initialize(const std::string& model_path, const EmbeddingConfig& config) {
    try {
        impl_->config_ = config;
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(4);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        impl_->session_ = std::make_unique<Ort::Session>(impl_->env_, model_path.c_str(), session_options);

        // Cache input/output names
        Ort::AllocatorWithDefaultOptions allocator;
        size_t num_inputs = impl_->session_->GetInputCount();
        for (size_t i = 0; i < num_inputs; ++i) {
            impl_->input_names_.push_back(std::string(impl_->session_->GetInputNameAllocated(i, allocator).get()));
        }
        size_t num_outputs = impl_->session_->GetOutputCount();
        for (size_t i = 0; i < num_outputs; ++i) {
            impl_->output_names_.push_back(std::string(impl_->session_->GetOutputNameAllocated(i, allocator).get()));
        }

        impl_->loaded_ = true;
        core::Logger::info("ONNX model loaded: " + model_path, {});
        return core::Result<void>();
    } catch (const Ort::Exception& e) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::ModelError,
            std::string("ONNX Runtime error: ") + e.what()));
    } catch (const std::exception& e) {
        return core::Result<void>(core::Error::from_code(core::ErrorCode::ModelError,
            std::string("ONNX initialization error: ") + e.what()));
    }
}

core::Result<std::vector<Embedding>> ONNXBackend::encode_images(std::span<const std::vector<uint8_t>> image_data, int width, int height) {
    if (!impl_->loaded_ || !impl_->session_) {
        return core::Result<std::vector<Embedding>>(core::Error::from_code(core::ErrorCode::ModelError, "ONNX model not loaded"));
    }
    try {
        std::vector<Embedding> results;
        Ort::AllocatorWithDefaultOptions allocator;

        for (size_t i = 0; i < image_data.size(); ++i) {
            // Create input tensor [1, 3, height, width] normalized to [0,1]
            std::vector<float> input_data;
            input_data.reserve(3 * height * width);
            const auto& rgb = image_data[i];
            for (int c = 0; c < 3; ++c) {
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        size_t idx = (y * width + x) * 3 + c;
                        float val = (idx < rgb.size()) ? static_cast<float>(rgb[idx]) / 255.0f : 0.0f;
                        input_data.push_back(val);
                    }
                }
            }

            std::vector<int64_t> input_shape = {1, 3, height, width};
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                allocator.GetInfo(), input_data.data(), input_data.size(), input_shape.data(), input_shape.size());

            Ort::RunOptions run_options;
            auto in_ptrs = impl_->input_name_ptrs();
            auto out_ptrs = impl_->output_name_ptrs();
            auto output_tensors = impl_->session_->Run(run_options,
                in_ptrs.data(), &input_tensor, 1,
                out_ptrs.data(), out_ptrs.size());

            float* output_data = output_tensors[0].GetTensorMutableData<float>();
            size_t output_count = output_tensors[0].GetTensorTypeAndShapeInfo().GetElementCount();

            Embedding emb{};
            emb.type = EmbeddingType::Global;
            emb.dim = static_cast<int>(output_count);
            emb.quant = impl_->config_.storage_quant;
            std::vector<float> float_emb(output_data, output_data + output_count);
            if (emb.quant == Quantization::INT8) {
                emb.int8_data = quantize_to_int8(float_emb);
            } else {
                emb.float_data = std::move(float_emb);
            }
            results.push_back(std::move(emb));
        }
        return core::Result<std::vector<Embedding>>(std::move(results));
    } catch (const Ort::Exception& e) {
        return core::Result<std::vector<Embedding>>(core::Error::from_code(core::ErrorCode::ModelError,
            std::string("ONNX image encode error: ") + e.what()));
    }
}

core::Result<std::vector<Embedding>> ONNXBackend::encode_text(const std::vector<std::string>& texts) {
    if (!impl_->loaded_ || !impl_->session_) {
        return core::Result<std::vector<Embedding>>(core::Error::from_code(core::ErrorCode::ModelError, "ONNX model not loaded"));
    }
    try {
        std::vector<Embedding> results;
        Ort::AllocatorWithDefaultOptions allocator;

        for (size_t i = 0; i < texts.size(); ++i) {
            // Simple char-level encoding: convert text to float tensor of token IDs
            // This is a placeholder text tokenizer for demonstration.
            // Real CLIP models use BPE tokenization.
            std::vector<float> token_ids(impl_->config_.text_dim, 0.0f);
            for (size_t j = 0; j < texts[i].size() && j < static_cast<size_t>(impl_->config_.text_dim); ++j) {
                token_ids[j] = static_cast<float>(static_cast<unsigned char>(texts[i][j])) / 255.0f;
            }

            std::vector<int64_t> input_shape = {1, impl_->config_.text_dim};
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                allocator.GetInfo(), token_ids.data(), token_ids.size(), input_shape.data(), input_shape.size());

            Ort::RunOptions run_options;
            auto in_ptrs = impl_->input_name_ptrs();
            auto out_ptrs = impl_->output_name_ptrs();
            auto output_tensors = impl_->session_->Run(run_options,
                in_ptrs.data(), &input_tensor, 1,
                out_ptrs.data(), out_ptrs.size());

            float* output_data = output_tensors[0].GetTensorMutableData<float>();
            size_t output_count = output_tensors[0].GetTensorTypeAndShapeInfo().GetElementCount();

            Embedding emb{};
            emb.type = EmbeddingType::Global;
            emb.dim = static_cast<int>(output_count);
            emb.quant = impl_->config_.storage_quant;
            std::vector<float> float_emb(output_data, output_data + output_count);
            if (emb.quant == Quantization::INT8) {
                emb.int8_data = quantize_to_int8(float_emb);
            } else {
                emb.float_data = std::move(float_emb);
            }
            results.push_back(std::move(emb));
        }
        return core::Result<std::vector<Embedding>>(std::move(results));
    } catch (const Ort::Exception& e) {
        return core::Result<std::vector<Embedding>>(core::Error::from_code(core::ErrorCode::ModelError,
            std::string("ONNX text encode error: ") + e.what()));
    }
}

void ONNXBackend::unload() {
    impl_->session_.reset();
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
