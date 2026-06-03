#pragma once

#include "video2vec/core/result.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace video2vec::ocr {

struct BBox {
    int x = 0, y = 0, w = 0, h = 0;
    [[nodiscard]] int area() const noexcept { return w * h; }
    [[nodiscard]] bool empty() const noexcept { return w <= 0 || h <= 0; }
};

struct OCRLine {
    BBox bbox;
    std::string text;
    double confidence = 0.0;
    bool pii_flagged = false;
};

struct OCRResult {
    std::vector<OCRLine> lines;
    int image_width = 0;
    int image_height = 0;
    double mean_confidence = 0.0;
};

class IOCRBackend {
public:
    virtual ~IOCRBackend() = default;
    virtual core::Result<void> initialize(const std::string& languages, const std::string& data_path = {}) = 0;
    virtual core::Result<OCRResult> recognize(std::span<const uint8_t> image_data, int width, int height, int channels) = 0;
    virtual void unload() = 0;
    [[nodiscard]] virtual bool is_loaded() const = 0;
    [[nodiscard]] virtual std::string name() const = 0;
};

} // namespace video2vec::ocr
