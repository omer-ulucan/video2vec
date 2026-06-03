#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace video2vec::vision {

struct Frame {
    int64_t pts_ms = 0;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb_data;
    bool is_keyframe = false;
    double entropy = 0.0;
    double ocr_density = 0.0;
    double score = 0.0;
};

struct Patch {
    int x = 0, y = 0, width = 0, height = 0;
    int64_t frame_pts_ms = 0;
    std::vector<uint8_t> png_data;
    double score = 0.0;
    std::string source;
};

} // namespace video2vec::vision
