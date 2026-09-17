#include "video2vec/vision/patch_extractor.hpp"
#include "video2vec/core/logger.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

#if defined(HAS_VIPS)
#include <vips/vips.h>
#endif

namespace video2vec::vision {

namespace {

bool has_rgb_pixels(const std::vector<uint8_t>& rgb_data, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    return rgb_data.size() >= static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
}

// ------------------------------------------------------------------
// Minimal dependency-free PNG writer (RGB8, stored/uncompressed deflate).
// Used when libvips is unavailable or fails, so png_data is always a
// valid PNG rather than raw RGB bytes labelled as one.
// ------------------------------------------------------------------
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[n] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

uint32_t adler32(const uint8_t* data, size_t len) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void put_u32be(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void write_chunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data) {
    put_u32be(out, static_cast<uint32_t>(data.size()));
    const size_t crc_start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put_u32be(out, crc32_update(0, out.data() + crc_start, out.size() - crc_start));
}

std::vector<uint8_t> encode_png_stored(const std::vector<uint8_t>& rgb_data, int width, int height) {
    const size_t row_bytes = static_cast<size_t>(width) * 3;
    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (row_bytes + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);  // filter type: none
        const uint8_t* row = rgb_data.data() + static_cast<size_t>(y) * row_bytes;
        raw.insert(raw.end(), row, row + row_bytes);
    }

    // zlib stream with stored (uncompressed) deflate blocks of <= 65535 bytes.
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    do {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n & 0xFF));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xFF));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    } while (pos < raw.size());
    put_u32be(z, adler32(raw.data(), raw.size()));

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    put_u32be(ihdr, static_cast<uint32_t>(width));
    put_u32be(ihdr, static_cast<uint32_t>(height));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // colour type: truecolour
    ihdr.push_back(0);  // compression
    ihdr.push_back(0);  // filter
    ihdr.push_back(0);  // interlace
    write_chunk(png, "IHDR", ihdr);
    write_chunk(png, "IDAT", z);
    write_chunk(png, "IEND", {});
    return png;
}

#if defined(HAS_VIPS)
bool ensure_vips() {
    static std::once_flag flag;
    static bool ok = false;
    std::call_once(flag, [] {
        // libvips must be initialized before any other call; previously it
        // was never initialized at all.
        ok = vips_init("video2vec") == 0;
        if (!ok) core::Logger::warn("vips_init failed; using the built-in PNG encoder", {});
    });
    return ok;
}

std::vector<uint8_t> encode_png_vips(const std::vector<uint8_t>& rgb_data, int width, int height) {
    if (!ensure_vips()) return {};
    VipsImage* in = vips_image_new_from_memory(
        const_cast<void*>(static_cast<const void*>(rgb_data.data())),
        static_cast<size_t>(width) * height * 3, width, height, 3, VIPS_FORMAT_UCHAR);
    if (!in) { vips_error_clear(); return {}; }
    void* buf = nullptr;
    size_t len = 0;
    int ret = vips_pngsave_buffer(in, &buf, &len, "compression", 3, "strip", TRUE, nullptr);
    g_object_unref(in);
    if (ret != 0 || !buf) {
        if (buf) g_free(buf);
        vips_error_clear();
        return {};
    }
    std::vector<uint8_t> result(static_cast<const uint8_t*>(buf), static_cast<const uint8_t*>(buf) + len);
    g_free(buf);
    return result;
}
#endif

} // namespace

std::vector<uint8_t> crop_rgb(const std::vector<uint8_t>& rgb_data, int src_width, int src_height,
                               int x, int y, int w, int h) {
    if (!has_rgb_pixels(rgb_data, src_width, src_height)) return {};
    x = std::max(0, x); y = std::max(0, y);
    w = std::min(w, src_width - x); h = std::min(h, src_height - y);
    if (w <= 0 || h <= 0) return {};
    std::vector<uint8_t> result(static_cast<size_t>(w) * h * 3);
    for (int row = 0; row < h; ++row) {
        size_t src_offset = (static_cast<size_t>(y + row) * src_width + x) * 3;
        size_t dst_offset = static_cast<size_t>(row) * w * 3;
        std::memcpy(result.data() + dst_offset, rgb_data.data() + src_offset, static_cast<size_t>(w) * 3);
    }
    return result;
}

std::vector<uint8_t> encode_png(const std::vector<uint8_t>& rgb_data, int width, int height) {
    if (!has_rgb_pixels(rgb_data, width, height)) return {};
#if defined(HAS_VIPS)
    auto png = encode_png_vips(rgb_data, width, height);
    if (!png.empty()) return png;
#endif
    return encode_png_stored(rgb_data, width, height);
}

std::vector<uint8_t> resize_rgb(const std::vector<uint8_t>& rgb_data, int width, int height, int out_width, int out_height) {
    if (!has_rgb_pixels(rgb_data, width, height) || out_width <= 0 || out_height <= 0) return {};
    std::vector<uint8_t> out(static_cast<size_t>(out_width) * out_height * 3);
    const double sx = static_cast<double>(width) / out_width;
    const double sy = static_cast<double>(height) / out_height;
    for (int oy = 0; oy < out_height; ++oy) {
        const double fy = std::min(static_cast<double>(height - 1), std::max(0.0, (oy + 0.5) * sy - 0.5));
        const int y0 = static_cast<int>(fy), y1 = std::min(height - 1, y0 + 1);
        const double wy = fy - y0;
        for (int ox = 0; ox < out_width; ++ox) {
            const double fx = std::min(static_cast<double>(width - 1), std::max(0.0, (ox + 0.5) * sx - 0.5));
            const int x0 = static_cast<int>(fx), x1 = std::min(width - 1, x0 + 1);
            const double wx = fx - x0;
            for (int c = 0; c < 3; ++c) {
                const double p00 = rgb_data[(static_cast<size_t>(y0) * width + x0) * 3 + c];
                const double p01 = rgb_data[(static_cast<size_t>(y0) * width + x1) * 3 + c];
                const double p10 = rgb_data[(static_cast<size_t>(y1) * width + x0) * 3 + c];
                const double p11 = rgb_data[(static_cast<size_t>(y1) * width + x1) * 3 + c];
                const double v = (p00 * (1 - wx) + p01 * wx) * (1 - wy) + (p10 * (1 - wx) + p11 * wx) * wy;
                out[(static_cast<size_t>(oy) * out_width + ox) * 3 + c] = static_cast<uint8_t>(v + 0.5);
            }
        }
    }
    return out;
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
    if (!has_rgb_pixels(frame.rgb_data, frame.width, frame.height)) return patches;

    for (const auto& line : ocr_lines) {
        if (patches.size() >= static_cast<size_t>(config.max_patches_per_frame)) break;
        auto expanded = expand_bbox(line.bbox, frame.width, frame.height, config.ocr_margin_percent);
        if (expanded.empty()) continue;
        auto cropped = crop_rgb(frame.rgb_data, frame.width, frame.height, expanded.x, expanded.y, expanded.w, expanded.h);
        if (cropped.empty()) continue;
        Patch patch{};
        patch.x = expanded.x; patch.y = expanded.y;
        patch.width = expanded.w; patch.height = expanded.h;
        patch.frame_pts_ms = frame.pts_ms;
        patch.png_data = encode_png(cropped, expanded.w, expanded.h);
        patch.score = line.confidence;
        patch.source = "ocr";
        patches.push_back(std::move(patch));
    }

    // Fill up to the configured minimum with a 3x3 grid. The last row/column
    // extends to the frame edge so no pixels are left uncovered, and frames
    // smaller than the grid collapse to a single cell.
    if (patches.size() < static_cast<size_t>(config.min_patches_per_frame)) {
        int grid_size = 3;
        if (frame.width < grid_size || frame.height < grid_size) grid_size = 1;
        const int cell_w = frame.width / grid_size;
        const int cell_h = frame.height / grid_size;
        const double entropy_score = std::clamp(frame.entropy / 8.0, 0.0, 1.0);  // same 0..1 scale as OCR confidence
        for (int gy = 0; gy < grid_size && patches.size() < static_cast<size_t>(config.min_patches_per_frame); ++gy) {
            for (int gx = 0; gx < grid_size && patches.size() < static_cast<size_t>(config.min_patches_per_frame); ++gx) {
                const int px = gx * cell_w, py = gy * cell_h;
                const int pw = (gx == grid_size - 1) ? frame.width - px : cell_w;
                const int ph = (gy == grid_size - 1) ? frame.height - py : cell_h;
                if (pw <= 0 || ph <= 0) continue;
                bool overlaps = false;
                for (const auto& p : patches) {
                    int inter_x = std::max(px, p.x), inter_y = std::max(py, p.y);
                    int inter_w = std::max(0, std::min(px + pw, p.x + p.width) - inter_x);
                    int inter_h = std::max(0, std::min(py + ph, p.y + p.height) - inter_y);
                    if (inter_w * inter_h > pw * ph / 2) { overlaps = true; break; }
                }
                if (overlaps) continue;
                auto cropped = crop_rgb(frame.rgb_data, frame.width, frame.height, px, py, pw, ph);
                if (cropped.empty()) continue;
                Patch patch{};
                patch.x = px; patch.y = py; patch.width = pw; patch.height = ph;
                patch.frame_pts_ms = frame.pts_ms;
                patch.png_data = encode_png(cropped, pw, ph);
                patch.score = entropy_score;
                patch.source = "entropy";
                patches.push_back(std::move(patch));
            }
        }
    }
    return patches;
}

} // namespace video2vec::vision
