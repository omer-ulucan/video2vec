#pragma once

#include "video2vec/vision/frame.hpp"
#include "video2vec/ocr/ocr_backend.hpp"
#include <vector>

namespace video2vec::vision {

struct PatchConfig {
    int target_size = 384;
    int max_patches_per_frame = 8;
    int min_patches_per_frame = 4;
    double ocr_margin_percent = 0.12;
};

std::vector<Patch> extract_patches(const Frame& frame, const std::vector<ocr::OCRLine>& ocr_lines,
                                    const PatchConfig& config);
std::vector<uint8_t> crop_rgb(const std::vector<uint8_t>& rgb_data, int src_width, int src_height,
                               int x, int y, int w, int h);
std::vector<uint8_t> encode_png(const std::vector<uint8_t>& rgb_data, int width, int height);
ocr::BBox expand_bbox(const ocr::BBox& bbox, int img_width, int img_height, double margin_percent);

} // namespace video2vec::vision
