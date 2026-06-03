#include "video2vec/ocr/tesseract_backend.hpp"
#include "video2vec/core/logger.hpp"
#include <tesseract/baseapi.h>
#include <leptonica/allheaders>
#include <vector>

namespace video2vec::ocr {

class TesseractBackend::Impl {
public:
    std::unique_ptr<tesseract::TessBaseAPI> api_;
    std::string languages_;
    bool loaded_ = false;
};

TesseractBackend::TesseractBackend() : impl_(std::make_unique<Impl>()) {}
TesseractBackend::~TesseractBackend() { unload(); }

core::Result<void> TesseractBackend::initialize(const std::string& languages, const std::string& data_path) {
    unload();
    impl_->api_ = std::make_unique<tesseract::TessBaseAPI>();
    const char* datapath = data_path.empty() ? nullptr : data_path.c_str();
    int ret = impl_->api_->Init(datapath, languages.c_str());
    if (ret != 0) {
        impl_->api_.reset();
        return core::Result<void>(core::Error::from_code(core::ErrorCode::ModelError,
            "failed to initialize Tesseract with languages: " + languages));
    }
    impl_->languages_ = languages;
    impl_->loaded_ = true;
    return core::Result<void>();
}

core::Result<OCRResult> TesseractBackend::recognize(std::span<const uint8_t> image_data, int width, int height, int channels) {
    if (!impl_->loaded_ || !impl_->api_) {
        return core::Result<OCRResult>(core::Error::from_code(core::ErrorCode::ModelError, "Tesseract not initialized"));
    }
    if (channels != 3 && channels != 4 && channels != 1) {
        return core::Result<OCRResult>(core::Error::from_code(core::ErrorCode::InvalidArgument,
            "unsupported channel count: " + std::to_string(channels)));
    }
    int bpp = channels * 8;
    Pix* pix = pixCreate(width, height, bpp);
    if (!pix) {
        return core::Result<OCRResult>(core::Error::from_code(core::ErrorCode::OutOfMemory, "failed to allocate Pix image"));
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            size_t idx = (y * width + x) * channels;
            if (channels == 1) pixSetPixel(pix, x, y, image_data[idx]);
            else {
                uint32_t val = (image_data[idx] << 16) | (image_data[idx + 1] << 8) | image_data[idx + 2];
                pixSetPixel(pix, x, y, val);
            }
        }
    }
    impl_->api_->SetImage(pix);
    impl_->api_->Recognize(nullptr);
    ResultIterator* ri = impl_->api_->GetIterator();
    PageIteratorLevel level = RIL_TEXTLINE;
    OCRResult result{};
    result.image_width = width;
    result.image_height = height;
    double total_conf = 0.0;
    int line_count = 0;
    if (ri) {
        do {
            char* text = ri->GetUTF8Text(level);
            if (!text) continue;
            float conf = ri->Confidence(level);
            int x1, y1, x2, y2;
            ri->BoundingBox(level, &x1, &y1, &x2, &y2);
            OCRLine line{};
            line.bbox = {x1, y1, x2 - x1, y2 - y1};
            line.text = text;
            line.confidence = conf / 100.0;
            result.lines.push_back(std::move(line));
            total_conf += conf;
            line_count++;
            delete[] text;
        } while (ri->Next(level));
        delete ri;
    }
    pixDestroy(&pix);
    if (line_count > 0) result.mean_confidence = total_conf / line_count / 100.0;
    return core::Result<OCRResult>(std::move(result));
}

void TesseractBackend::unload() {
    if (impl_->api_) { impl_->api_->End(); impl_->api_.reset(); }
    impl_->loaded_ = false;
}

bool TesseractBackend::is_loaded() const { return impl_->loaded_; }

} // namespace video2vec::ocr
