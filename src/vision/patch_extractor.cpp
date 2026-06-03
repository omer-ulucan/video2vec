#include "video2vec/vision/patch_extractor.hpp"
#include "video2vec/core/logger.hpp"
#include <algorithm>
#include <cstring>

#if defined(HAS_VIPS)
#include <vips/vips.h>
#endif

namespace video2vec::vision {

std::vector<uint8_t> crop_rgb(const std::vector<uint8_t>& rgb_data, int src_width, int src_height,
                               int x, int y, int w, int h) {
    x = std::max(0, x); y = std::max(0, y);
    w = std::min(w, src_width - x); h = std::min(h, src_height - y);
    if (w <= 0 || h <= 0) return {};
    std::vector<uint8_t> result(w * h * 3);
    for (int row = 0; row < h; ++row) {
        size_t src_offset = ((y + row) * src_width + x) * 3;
        size_t dst_offset = row * w * 3;
        std::memcpy(result.data() + dst_offset, rgb_data.data() + src_offset, w * 3);
    }
    return result;
}

std::vector<uint8_t> encode_png(const std::vector<uint8_t>& rgb_data, int width, int height) {
    if (rgb_data.empty() || width <= 0 || height <= 0) return {};
#if defined(HAS_VIPS)
    try {
        VipsImage* in = vips_image_new_from_memory(
            const_cast<void*>(static_cast<const void*>(rgb_data.data())), rgb_data.size(), width, height, 3, VIPS_FORMAT_UCHAR);
        if (!in) return {};
        void* buf = nullptr; size_t len = 0;
        int ret = vips_pngsave_buffer(in, &buf, &len, "compression", 3, "strip", TRUE, nullptr);
        g_object_unref(in);
        if (ret != 0 || !buf) return {};
        std::vector<uint8_t> result(len);
        std::memcpy(result.data(), buf, len);
        g_free(buf);
        return result;
    } catch (...) {}
#endif
    return rgb_data;
}

ocr::BBox expand_bbox(const ocr::BBox& bbox, int img_width, int img_height, double margin_percent) {
    int margin_x = static_cast<int>(bbox.w * margin_percent);
    int margin_y = static_cast<int>(bbox.h * margin_percent);
    ocr::BBox expanded{};
    expanded.x = std::max(0, bbox.x - margin_x);
    expanded.y = std::max(0, bbox.y - margin_y);
    expanded.w = std::min(img_width - expanded.x, bbox.w + 2 * margin_x);
    expanded.h = std::min(img_height - expanded.y, bbox.h + 2 * margin_y);
    return expanded;
}

std::vector<Patch> extract_patches(const Frame& frame, const std::vector<ocr::OCRLine>& ocr_lines,
                                    const PatchConfig& config) {
    std::vector<Patch> patches;
    for (const auto& line : ocr_lines) {
        if (patches.size() >= static_cast<size_t>(config.max_patches_per_frame)) break;
        auto expanded = expand_bbox(line.bbox, frame.width, frame.height, config.ocr_margin_percent);
        if (expanded.empty()) continue;
        auto cropped = crop_rgb(frame.rgb_data, frame.width, frame.height, expanded.x, expanded.y, expanded.w, expanded.h);
        auto png = encode_png(cropped, expanded.w, expanded.h);
        Patch patch{};
        patch.x = expanded.x; patch.y = expanded.y;
        patch.width = expanded.w; patch.height = expanded.h;
        patch.frame_pts_ms = frame.pts_ms;
        patch.png_data = std::move(png);
        patch.score = line.confidence;
        patch.source = "ocr";
        patches.push_back(std::move(patch));
    }
    if (patches.size() < static_cast<size_t>(config.min_patches_per_frame)) {
        int grid_size = 3;
        int cell_w = frame.width / grid_size;
        int cell_h = frame.height / grid_size;
        for (int gy = 0; gy < grid_size && patches.size() < static_cast<size_t>(config.max_patches_per_frame); ++gy) {
            for (int gx = 0; gx < grid_size && patches.size() < static_cast<size_t>(config.max_patches_per_frame); ++gx) {
                int px = gx * cell_w, py = gy * cell_h;
                int pw = std::min(cell_w, frame.width - px);
                int ph = std::min(cell_h, frame.height - py);
                bool overlaps = false;
                for (const auto& p : patches) {
                    int inter_x = std::max(px, p.x), inter_y = std::max(py, p.y);
                    int inter_w = std::max(0, std::min(px + pw, p.x + p.width) - inter_x);
                    int inter_h = std::max(0, std::min(py + ph, p.y + p.height) - inter_y);
                    if (inter_w * inter_h > pw * ph / 2) { overlaps = true; break; }
                }
                if (overlaps) continue;
                auto cropped = crop_rgb(frame.rgb_data, frame.width, frame.height, px, py, pw, ph);
                auto png = encode_png(cropped, pw, ph);
                Patch patch{};
                patch.x = px; patch.y = py; patch.width = pw; patch.height = ph;
                patch.frame_pts_ms = frame.pts_ms;
                patch.png_data = std::move(png);
                patch.score = frame.entropy;
                patch.source = "entropy";
                patches.push_back(std::move(patch));
            }
        }
    }
    return patches;
}

} // namespace video2vec::vision
