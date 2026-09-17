#pragma once

#include "video2vec/core/result.hpp"
#include "video2vec/version.hpp"
#include "video2vec/windowing/window.hpp"
#include "video2vec/vision/frame.hpp"
#include "video2vec/ocr/ocr_backend.hpp"
#include "video2vec/embedding/embedding_backend.hpp"
#include "video2vec/asr/asr_backend.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The .vec container.
//
// Despite the module name, this is a hand-rolled little-endian binary format;
// schema.fbs documents the intended data model but is not compiled (no flatc
// step exists). Layout:
//
//   u32 magic (0x56454321)
//   u32 format version (kVecFormatVersion)
//   str producer version
//   u32 window count, then per window:
//     u64 t0_ms, u64 t1_ms, u32 index, str transcript
//     u32 words   { u64 t_ms, u64 dt_ms, str text, f64 confidence }
//     u32 frames  { u64 pts_ms, u32 width, u32 height, u8 keyframe,
//                   f64 entropy, f64 ocr_density, f64 score, bytes rgb }
//     u32 ocr     { u32 x, y, w, h, str text, f64 confidence, u8 pii_flagged }
//     u32 embeds  { u8 type, u8 quant, u32 dim, f32 int8_scale, bytes int8, f32[] floats }
//     u64 processing_time_ms
//
// str = u32 length + bytes; bytes = u32 length + bytes. Readers validate
// every length against the remaining input and reject other format versions.
namespace video2vec::flatbuffers {

constexpr uint32_t kVecFormatVersion = 2;

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

core::Result<std::vector<uint8_t>> package_windows(const std::vector<PackagedWindow>& windows,
                                                   const std::string& version = VIDEO2VEC_VERSION_STRING);
core::Result<std::vector<PackagedWindow>> unpack_windows(const std::vector<uint8_t>& data);
core::Result<void> write_index(const std::vector<PackagedWindow>& windows, const std::string& path);

} // namespace video2vec::flatbuffers
