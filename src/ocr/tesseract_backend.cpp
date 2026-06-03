#include "video2vec/ocr/tesseract_backend.hpp"
#include "video2vec/core/logger.hpp"
#include <cstring>
#include <vector>

#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

namespace video2vec::ocr {

class TesseractBackend::Impl {
public:
    std::unique_ptr<tesseract::TessBaseAPI> api;
    std::string languages;
    std::string data_path;
    bool loaded = false;
};

TesseractBackend::TesseractBackend() : impl_(std::make_unique<Impl>()) {}
TesseractBackend::~TesseractBackend() { unload(); }

core::Result<void> TesseractBackend::initialize(const std::string& languages, const std::string& data_path) {
    unload();
    impl_->languages = languages;
    impl_->data_path = data_path;
    impl_->api = std::make_unique<tesseract::TessBaseAPI>();

    const char* datapath = data_path.empty() ? nullptr : data_path.c_str();
    int ret = impl_->api->Init(datapath, languages.c_str());
    if (ret != 0) {
        impl_->api.reset();
        return core::Result<void>(core::Error::from_code(core::ErrorCode::ModelError,
            "Tesseract init failed for languages: " + languages));
    }
    impl_->loaded = true;
    core::Logger::info("Tesseract initialized with languages: " + languages, {});
    return core::Result<void>();
}

core::Result<OCRResult> TesseractBackend::recognize(std::span<const uint8_t> image_data, int width, int height, int channels) {
    if (!impl_->loaded || !impl_->api) {
        return core::Result<OCRResult>(core::Error::from_code(core::ErrorCode::ModelError, "Tesseract not initialized"));
    }
    if (channels != 1 && channels != 3 && channels != 4) {
        return core::Result<OCRResult>(core::Error::from_code(core::ErrorCode::InvalidArgument,
            "Unsupported channel count: " + std::to_string(channels)));
    }

    Pix* pix = nullptr;
    if (channels == 1) {
        pix = pixCreate(width, height, 8);
        if (!pix) return core::Result<OCRResult>(core::Error::from_code(core::ErrorCode::InternalError, "pixCreate failed"));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                size_t idx = y * width + x;
                pixSetPixel(pix, x, y, idx < image_data.size() ? image_data[idx] : 0);
            }
        }
    } else {
        // Convert RGB/RGBA to grayscale 8-bit for Tesseract
        pix = pixCreate(width, height, 8);
        if (!pix) return core::Result<OCRResult>(core::Error::from_code(core::ErrorCode::InternalError, "pixCreate failed"));
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                size_t idx = (y * width + x) * channels;
                uint8_t gray = 0;
                if (idx + 2 < image_data.size()) {
                    gray = static_cast<uint8_t>(0.299 * image_data[idx] +
                                                 0.587 * image_data[idx + 1] +
                                                 0.114 * image_data[idx + 2]);
                }
                pixSetPixel(pix, x, y, gray);
            }
        }
    }

    impl_->api->SetImage(pix);
    char* outText = impl_->api->GetUTF8Text();
    int conf = impl_->api->MeanTextConf();

    OCRResult result{};
    result.image_width = width;
    result.image_height = height;
    if (outText) {
        std::string text = outText;
        while (!text.empty() && std::isspace(text.back())) text.pop_back();
        delete[] outText;
        OCRLine line{};
        line.bbox = {0, 0, width, height};
        line.text = text;
        line.confidence = conf / 100.0;
        result.lines.push_back(std::move(line));
    }
    result.mean_confidence = conf / 100.0;
    pixDestroy(&pix);
    return core::Result<OCRResult>(std::move(result));
}

void TesseractBackend::unload() {
    if (impl_->api) {
        impl_->api->End();
        impl_->api.reset();
    }
    impl_->loaded = false;
}

bool TesseractBackend::is_loaded() const { return impl_->loaded; }

} // namespace video2vec::ocr
