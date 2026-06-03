#pragma once

#include "video2vec/core/result.hpp"
#include "video2vec/windowing/window.hpp"
#include "video2vec/vision/frame.hpp"
#include "video2vec/ocr/ocr_backend.hpp"
#include "video2vec/embedding/embedding_backend.hpp"
#include "video2vec/asr/asr_backend.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace video2vec::flatbuffers {

struct PackagedWindow {
    int64_t t0_ms = 0;
    int64_t t1_ms = 0;
    int index = 0;
    std::string transcript;
    std::vector<asr::ASRWord> words;
    std::vector<vision::Frame> frames;
    std::vector<ocr::OCRLine> ocr_lines;
    std::vector<embedding::Embedding> embeddings;
    int64_t processing_time_ms = 0;
};

core::Result<std::vector<uint8_t>> package_windows(const std::vector<PackagedWindow>& windows, const std::string& version = "1.0.0");
core::Result<std::vector<PackagedWindow>> unpack_windows(const std::vector<uint8_t>& data);
core::Result<void> write_index(const std::vector<PackagedWindow>& windows, const std::string& path);

} // namespace video2vec::flatbuffers
