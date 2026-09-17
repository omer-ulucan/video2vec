#include "video2vec/embedding/onnx_backend.hpp"
#include "video2vec/core/logger.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

#include "onnxruntime_cxx_api.h"

namespace video2vec::embedding {

namespace {
    std::string shape_to_string(const std::vector<int64_t>& shape) {
        std::ostringstream out;
        out << "[";
        for (size_t i = 0; i < shape.size(); ++i) out << (i ? "," : "") << shape[i];
        out << "]";
        return out.str();
    }

    template <typename T>
    core::Result<T> model_error(const std::string& what) {
        return core::Result<T>(core::Error::from_code(core::ErrorCode::ModelError, what));
    }
}

class ONNXBackend::Impl {
public:
    EmbeddingConfig config_;
    bool loaded_ = false;
    Ort::Env env_{ORT_LOGGING_LEVEL_WARNING, "video2vec"};
    std::unique_ptr<Ort::Session> session_;
    // Name storage and the const char* views handed to Session::Run. The
    // pointer arrays are built only after the name vectors are complete:
    // taking c_str() while still pushing into a vector<string> left dangling
    // pointers once the vector reallocated.
    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;
    std::vector<const char*> input_ptrs_;
    std::vector<const char*> output_ptrs_;
    ONNXTensorElementDataType input_type_ = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
    std::vector<int64_t> input_shape_;  // -1 for dynamic dimensions

    void clear() {
        session_.reset();
        input_names_.clear();
        output_names_.clear();
        input_ptrs_.clear();
        output_ptrs_.clear();
        input_type_ = ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
        input_shape_.clear();
        loaded_ = false;
    }

    // Converts the first output tensor of a Run() into an Embedding.
    Embedding to_embedding(Ort::Value& output, EmbeddingType type) const {
        float* output_data = output.GetTensorMutableData<float>();
        size_t output_count = output.GetTensorTypeAndShapeInfo().GetElementCount();
        Embedding emb{};
        emb.type = type;
        emb.dim = static_cast<int>(output_count);
        emb.quant = config_.storage_quant;
        std::vector<float> float_emb(output_data, output_data + output_count);
        if (emb.quant == Quantization::INT8) {
            emb.int8_data = quantize_to_int8(float_emb, emb.int8_scale);
        } else {
            emb.float_data = std::move(float_emb);
            emb.int8_scale = 1.0f;
        }
        return emb;
    }
};

ONNXBackend::ONNXBackend() : impl_(std::make_unique<Impl>()) {}
ONNXBackend::~ONNXBackend() { unload(); }

core::Result<void> ONNXBackend::initialize(const std::string& model_path, const EmbeddingConfig& config) {
    unload();  // a second initialize() must not append to the previous session's name tables
    try {
        impl_->config_ = config;
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(4);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        impl_->session_ = std::make_unique<Ort::Session>(impl_->env_, model_path.c_str(), session_options);

        Ort::AllocatorWithDefaultOptions allocator;
        const size_t num_inputs = impl_->session_->GetInputCount();
        const size_t num_outputs = impl_->session_->GetOutputCount();
        if (num_inputs != 1 || num_outputs < 1) {
            impl_->clear();
            return core::Result<void>(core::Error::from_code(core::ErrorCode::Unsupported,
                "ONNX model must have exactly one input and at least one output (has " +
                std::to_string(num_inputs) + " inputs, " + std::to_string(num_outputs) + " outputs): " + model_path));
        }
        for (size_t i = 0; i < num_inputs; ++i) {
            impl_->input_names_.emplace_back(impl_->session_->GetInputNameAllocated(i, allocator).get());
        }
        for (size_t i = 0; i < num_outputs; ++i) {
            impl_->output_names_.emplace_back(impl_->session_->GetOutputNameAllocated(i, allocator).get());
        }
        for (const auto& n : impl_->input_names_) impl_->input_ptrs_.push_back(n.c_str());
        for (const auto& n : impl_->output_names_) impl_->output_ptrs_.push_back(n.c_str());

        // GetTensorTypeAndShapeInfo() returns an unowned view into the TypeInfo,
        // so the TypeInfo must outlive every use of the view.
        Ort::TypeInfo input_type_info = impl_->session_->GetInputTypeInfo(0);
        auto input_info = input_type_info.GetTensorTypeAndShapeInfo();
        impl_->input_type_ = input_info.GetElementType();
        impl_->input_shape_ = input_info.GetShape();

        impl_->loaded_ = true;
        core::Logger::info("ONNX model loaded: " + model_path + " input " + impl_->input_names_[0] +
                           shape_to_string(impl_->input_shape_), {});
        return core::Result<void>();
    } catch (const Ort::Exception& e) {
        impl_->clear();
        return core::Result<void>(core::Error::from_code(core::ErrorCode::ModelError,
            std::string("ONNX Runtime error: ") + e.what()));
    } catch (const std::exception& e) {
        impl_->clear();
        return core::Result<void>(core::Error::from_code(core::ErrorCode::ModelError,
            std::string("ONNX initialization error: ") + e.what()));
    }
}

core::Result<std::vector<Embedding>> ONNXBackend::encode_images(std::span<const std::vector<uint8_t>> image_data, int width, int height) {
    using R = core::Result<std::vector<Embedding>>;
    if (!impl_->loaded_ || !impl_->session_) return model_error<std::vector<Embedding>>("ONNX model not loaded");
    if (width <= 0 || height <= 0) {
        return R(core::Error::from_code(core::ErrorCode::InvalidArgument, "image dimensions must be positive"));
    }
    if (impl_->input_type_ != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        return R(core::Error::from_code(core::ErrorCode::Unsupported,
            "image model input must be a float tensor (got ONNX element type " + std::to_string(impl_->input_type_) + ")"));
    }
    const std::vector<int64_t> input_shape = {1, 3, height, width};
    const auto& expected = impl_->input_shape_;
    if (expected.size() != 4) {
        return R(core::Error::from_code(core::ErrorCode::Unsupported,
            "image model input must be a 4-D [N,3,H,W] tensor, model declares " + shape_to_string(expected)));
    }
    for (size_t d = 0; d < 4; ++d) {
        if (expected[d] > 0 && expected[d] != input_shape[d]) {
            return R(core::Error::from_code(core::ErrorCode::InvalidArgument,
                "image " + shape_to_string(input_shape) + " does not match the model input " + shape_to_string(expected)));
        }
    }
    const size_t expected_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;

    try {
        std::vector<Embedding> results;
        results.reserve(image_data.size());
        Ort::AllocatorWithDefaultOptions allocator;
        std::vector<float> input_data(expected_bytes);

        for (size_t i = 0; i < image_data.size(); ++i) {
            const auto& rgb = image_data[i];
            if (rgb.size() != expected_bytes) {
                return R(core::Error::from_code(core::ErrorCode::InvalidArgument,
                    "image " + std::to_string(i) + " has " + std::to_string(rgb.size()) + " bytes, expected " +
                    std::to_string(expected_bytes) + " for " + std::to_string(width) + "x" + std::to_string(height) + " RGB"));
            }
            // Packed RGB -> planar [1, 3, H, W] scaled to [0, 1]. No model-specific
            // mean/std normalization is applied; see onnx_backend.hpp.
            const size_t plane = static_cast<size_t>(width) * static_cast<size_t>(height);
            for (size_t p = 0; p < plane; ++p) {
                input_data[p] = static_cast<float>(rgb[p * 3]) / 255.0f;
                input_data[plane + p] = static_cast<float>(rgb[p * 3 + 1]) / 255.0f;
                input_data[2 * plane + p] = static_cast<float>(rgb[p * 3 + 2]) / 255.0f;
            }

            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                allocator.GetInfo(), input_data.data(), input_data.size(), input_shape.data(), input_shape.size());
            Ort::RunOptions run_options;
            auto output_tensors = impl_->session_->Run(run_options,
                impl_->input_ptrs_.data(), &input_tensor, 1,
                impl_->output_ptrs_.data(), impl_->output_ptrs_.size());
            results.push_back(impl_->to_embedding(output_tensors[0], EmbeddingType::Global));
        }
        return R(std::move(results));
    } catch (const Ort::Exception& e) {
        return model_error<std::vector<Embedding>>(std::string("ONNX image encode error: ") + e.what());
    } catch (const std::exception& e) {
        return model_error<std::vector<Embedding>>(std::string("ONNX image encode error: ") + e.what());
    }
}

core::Result<std::vector<Embedding>> ONNXBackend::encode_text(const std::vector<std::string>& texts) {
    using R = core::Result<std::vector<Embedding>>;
    if (!impl_->loaded_ || !impl_->session_) return model_error<std::vector<Embedding>>("ONNX model not loaded");
    if (impl_->input_type_ != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        // Transformer text encoders take int64 token ids produced by a
        // tokenizer this build does not ship; refuse instead of feeding bytes.
        return R(core::Error::from_code(core::ErrorCode::Unsupported,
            "text model input is not a float tensor; token-id models require a tokenizer, which is not implemented"));
    }
    if (impl_->input_shape_.size() != 2) {
        return R(core::Error::from_code(core::ErrorCode::Unsupported,
            "text model input must be a 2-D [N, dim] float tensor, model declares " + shape_to_string(impl_->input_shape_)));
    }
    // The model's static feature dimension wins over the configured text_dim.
    int64_t seq_len = impl_->input_shape_[1] > 0 ? impl_->input_shape_[1] : impl_->config_.text_dim;
    if (seq_len <= 0) {
        return R(core::Error::from_code(core::ErrorCode::InvalidArgument, "text_dim must be positive for a dynamic-width text model"));
    }

    try {
        std::vector<Embedding> results;
        results.reserve(texts.size());
        Ort::AllocatorWithDefaultOptions allocator;
        const std::vector<int64_t> input_shape = {1, seq_len};
        std::vector<float> features(static_cast<size_t>(seq_len));

        for (size_t i = 0; i < texts.size(); ++i) {
            // Placeholder character-level encoding (byte / 255 per position);
            // see the note in onnx_backend.hpp.
            std::fill(features.begin(), features.end(), 0.0f);
            const size_t n = std::min(texts[i].size(), features.size());
            for (size_t j = 0; j < n; ++j) {
                features[j] = static_cast<float>(static_cast<unsigned char>(texts[i][j])) / 255.0f;
            }
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                allocator.GetInfo(), features.data(), features.size(), input_shape.data(), input_shape.size());
            Ort::RunOptions run_options;
            auto output_tensors = impl_->session_->Run(run_options,
                impl_->input_ptrs_.data(), &input_tensor, 1,
                impl_->output_ptrs_.data(), impl_->output_ptrs_.size());
            results.push_back(impl_->to_embedding(output_tensors[0], EmbeddingType::Global));
        }
        return R(std::move(results));
    } catch (const Ort::Exception& e) {
        return model_error<std::vector<Embedding>>(std::string("ONNX text encode error: ") + e.what());
    } catch (const std::exception& e) {
        return model_error<std::vector<Embedding>>(std::string("ONNX text encode error: ") + e.what());
    }
}

void ONNXBackend::unload() {
    impl_->clear();
}

bool ONNXBackend::is_loaded() const { return impl_->loaded_; }

std::vector<int8_t> quantize_to_int8(std::span<const float> data, float& scale) {
    scale = 1.0f;
    if (data.empty()) return {};
    float max_val = 0.0f;
    for (float v : data) max_val = std::max(max_val, std::abs(v));
    if (max_val == 0.0f) max_val = 1.0f;
    scale = max_val;
    std::vector<int8_t> result(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        float scaled = data[i] / max_val * 127.0f;
        result[i] = static_cast<int8_t>(std::lround(std::clamp(scaled, -127.0f, 127.0f)));
    }
    return result;
}

std::vector<int8_t> quantize_to_int8(std::span<const float> data) {
    float scale = 1.0f;
    return quantize_to_int8(data, scale);
}

std::vector<float> dequantize_from_int8(std::span<const int8_t> data, float scale) {
    std::vector<float> result(data.size());
    for (size_t i = 0; i < data.size(); ++i) result[i] = static_cast<float>(data[i]) / 127.0f * scale;
    return result;
}

std::vector<float> to_float(const Embedding& embedding) {
    if (!embedding.float_data.empty()) return embedding.float_data;
    if (!embedding.int8_data.empty()) {
        return dequantize_from_int8(embedding.int8_data, embedding.int8_scale > 0.0f ? embedding.int8_scale : 1.0f);
    }
    return {};
}

} // namespace video2vec::embedding
