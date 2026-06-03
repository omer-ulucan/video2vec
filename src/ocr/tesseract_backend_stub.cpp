#include "video2vec/ocr/tesseract_backend.hpp"
#include "video2vec/core/logger.hpp"

namespace video2vec::ocr {

class TesseractBackend::Impl {
public:
    std::string languages_;
    bool loaded_ = false;
};

TesseractBackend::TesseractBackend() : impl_(std::make_unique<Impl>()) {}
TesseractBackend::~TesseractBackend() { unload(); }

core::Result<void> TesseractBackend::initialize(const std::string& languages, const std::string& data_path) {
    (void)languages; (void)data_path;
    return core::Result<void>(core::Error::from_code(core::ErrorCode::Unsupported,
        "Tesseract backend not available (compiled without Tesseract)"));
}

core::Result<OCRResult> TesseractBackend::recognize(std::span<const uint8_t> image_data, int width, int height, int channels) {
    (void)image_data; (void)width; (void)height; (void)channels;
    return core::Result<OCRResult>(core::Error::from_code(core::ErrorCode::Unsupported,
        "Tesseract backend not available (compiled without Tesseract)"));
}

void TesseractBackend::unload() { impl_->loaded_ = false; }
bool TesseractBackend::is_loaded() const { return impl_->loaded_; }

} // namespace video2vec::ocr
