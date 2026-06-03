#pragma once

#include "video2vec/ocr/ocr_backend.hpp"
#include <memory>

namespace video2vec::ocr {

class TesseractBackend : public IOCRBackend {
public:
    TesseractBackend();
    ~TesseractBackend() override;
    TesseractBackend(const TesseractBackend&) = delete;
    TesseractBackend& operator=(const TesseractBackend&) = delete;
    core::Result<void> initialize(const std::string& languages, const std::string& data_path = {}) override;
    core::Result<OCRResult> recognize(std::span<const uint8_t> image_data, int width, int height, int channels) override;
    void unload() override;
    [[nodiscard]] bool is_loaded() const override;
    [[nodiscard]] std::string name() const override { return "tesseract"; }
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace video2vec::ocr
