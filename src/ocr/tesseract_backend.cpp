#include "video2vec/ocr/tesseract_backend.hpp"
#include "video2vec/core/logger.hpp"
#include <cctype>
#include <cstring>
#include <memory>
#include <vector>

#include <tesseract/baseapi.h>
#include <tesseract/resultiterator.h>
#include <leptonica/allheaders.h>

namespace video2vec::ocr {

namespace {
    struct PixDeleter {
        void operator()(Pix* pix) const noexcept { if (pix) pixDestroy(&pix); }
    };
    using PixPtr = std::unique_ptr<Pix, PixDeleter>;

    std::string trim_right(const char* text) {
        std::string s = text ? text : "";
        // UTF-8 continuation bytes are >= 0x80: cast to unsigned char before
        // std::isspace, which is undefined for negative values.
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
        return s;
    }

    // Builds an 8-bit grayscale Pix from packed 1/3/4-channel image data.
    PixPtr to_gray_pix(std::span<const uint8_t> image_data, int width, int height, int channels) {
        PixPtr pix(pixCreate(width, height, 8));
        if (!pix) return nullptr;
        pixSetResolution(pix.get(), 96, 96);  // avoids Tesseract's "invalid resolution" warning
        l_uint32* data = pixGetData(pix.get());
        const int wpl = pixGetWpl(pix.get());
        for (int y = 0; y < height; ++y) {
            l_uint32* line = data + static_cast<size_t>(y) * wpl;
            const uint8_t* src = image_data.data() + static_cast<size_t>(y) * width * channels;
            for (int x = 0; x < width; ++x) {
                uint8_t gray;
                if (channels == 1) {
                    gray = src[x];
                } else {
                    const uint8_t* px = src + static_cast<size_t>(x) * channels;
                    gray = static_cast<uint8_t>(0.299 * px[0] + 0.587 * px[1] + 0.114 * px[2]);
                }
                SET_DATA_BYTE(line, x, gray);
            }
        }
        return pix;
    }
}

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
    using R = core::Result<OCRResult>;
    if (!impl_->loaded || !impl_->api) {
        return R(core::Error::from_code(core::ErrorCode::ModelError, "Tesseract not initialized"));
    }
    if (channels != 1 && channels != 3 && channels != 4) {
        return R(core::Error::from_code(core::ErrorCode::InvalidArgument,
            "Unsupported channel count: " + std::to_string(channels)));
    }
    if (width <= 0 || height <= 0) {
        return R(core::Error::from_code(core::ErrorCode::InvalidArgument, "image dimensions must be positive"));
    }
    const size_t needed = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
    if (image_data.size() < needed) {
        // Previously short buffers were zero-filled and OCR'd as a black image.
        return R(core::Error::from_code(core::ErrorCode::InvalidArgument,
            "image buffer has " + std::to_string(image_data.size()) + " bytes, expected at least " +
            std::to_string(needed) + " for " + std::to_string(width) + "x" + std::to_string(height) + "x" +
            std::to_string(channels)));
    }

    PixPtr pix = to_gray_pix(image_data, width, height, channels);
    if (!pix) return R(core::Error::from_code(core::ErrorCode::InternalError, "pixCreate failed"));

    tesseract::TessBaseAPI& api = *impl_->api;
    api.SetImage(pix.get());
    if (api.Recognize(nullptr) != 0) {
        api.Clear();
        return R(core::Error::from_code(core::ErrorCode::InternalError, "Tesseract recognition failed"));
    }

    OCRResult result{};
    result.image_width = width;
    result.image_height = height;

    // Per-line text, bounding box and confidence via the result iterator.
    // The previous implementation returned the whole page as a single line
    // with a full-image bounding box, which made every OCR patch the frame.
    std::unique_ptr<tesseract::ResultIterator> it(api.GetIterator());
    if (it) {
        const tesseract::PageIteratorLevel level = tesseract::RIL_TEXTLINE;
        do {
            std::unique_ptr<char[]> text(it->GetUTF8Text(level));
            std::string line_text = trim_right(text.get());
            if (line_text.empty()) continue;
            OCRLine line{};
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            if (it->BoundingBox(level, &x1, &y1, &x2, &y2)) {
                line.bbox = {x1, y1, x2 - x1, y2 - y1};
            } else {
                line.bbox = {0, 0, width, height};
            }
            line.text = std::move(line_text);
            line.confidence = static_cast<double>(it->Confidence(level)) / 100.0;
            result.lines.push_back(std::move(line));
        } while (it->Next(level));
    } else {
        std::unique_ptr<char[]> text(api.GetUTF8Text());
        std::string page = trim_right(text.get());
        if (!page.empty()) {
            OCRLine line{};
            line.bbox = {0, 0, width, height};
            line.text = std::move(page);
            line.confidence = api.MeanTextConf() / 100.0;
            result.lines.push_back(std::move(line));
        }
    }
    result.mean_confidence = result.lines.empty() ? 0.0 : api.MeanTextConf() / 100.0;
    api.Clear();  // release the image and recognition results held by the API
    return R(std::move(result));
}

void TesseractBackend::unload() {
    impl_->loaded = false;
    if (impl_->api) {
        try {
            impl_->api->End();
        } catch (...) {
            // swallow exceptions during cleanup
        }
        impl_->api.reset();
    }
}

bool TesseractBackend::is_loaded() const { return impl_->loaded; }

} // namespace video2vec::ocr
