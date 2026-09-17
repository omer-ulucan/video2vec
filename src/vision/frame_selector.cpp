#include "video2vec/vision/frame_selector.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <span>

namespace video2vec::vision {

namespace {
    // True when `rgb_data` holds at least width*height packed RGB pixels.
    bool has_rgb_pixels(const std::vector<uint8_t>& rgb_data, int width, int height) {
        if (width <= 0 || height <= 0) return false;
        const size_t needed = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
        return rgb_data.size() >= needed;
    }

    // Entropy of an 8-bit grayscale histogram is at most 8 bits.
    constexpr double kMaxEntropyBits = 8.0;
}

double compute_entropy(const std::vector<uint8_t>& rgb_data, int width, int height) {
    if (!has_rgb_pixels(rgb_data, width, height)) return 0.0;
    std::vector<int> hist(256, 0);
    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < pixel_count; ++i) {
        size_t idx = i * 3;
        uint8_t gray = static_cast<uint8_t>(0.299 * rgb_data[idx] + 0.587 * rgb_data[idx + 1] + 0.114 * rgb_data[idx + 2]);
        hist[gray]++;
    }
    double entropy = 0.0;
    for (int count : hist) {
        if (count == 0) continue;
        double p = static_cast<double>(count) / static_cast<double>(pixel_count);
        entropy -= p * std::log2(p);
    }
    return entropy;
}

double compute_ssim(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int width, int height) {
    if (a.size() != b.size() || !has_rgb_pixels(a, width, height)) return 0.0;
    const double C1 = 6.5025, C2 = 58.5225;
    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    double sum_a = 0.0, sum_b = 0.0, sum_a2 = 0.0, sum_b2 = 0.0, sum_ab = 0.0;
    for (size_t i = 0; i < pixel_count; ++i) {
        size_t idx = i * 3;
        double ya = 0.299 * a[idx] + 0.587 * a[idx + 1] + 0.114 * a[idx + 2];
        double yb = 0.299 * b[idx] + 0.587 * b[idx + 1] + 0.114 * b[idx + 2];
        sum_a += ya; sum_b += yb;
        sum_a2 += ya * ya; sum_b2 += yb * yb;
        sum_ab += ya * yb;
    }
    const double n = static_cast<double>(pixel_count);
    double mu_a = sum_a / n, mu_b = sum_b / n;
    double sigma_a2 = sum_a2 / n - mu_a * mu_a;
    double sigma_b2 = sum_b2 / n - mu_b * mu_b;
    double sigma_ab = sum_ab / n - mu_a * mu_b;
    double numerator = (2.0 * mu_a * mu_b + C1) * (2.0 * sigma_ab + C2);
    double denominator = (mu_a * mu_a + mu_b * mu_b + C1) * (sigma_a2 + sigma_b2 + C2);
    if (denominator == 0.0) return 1.0;
    return numerator / denominator;
}

double cosine_similarity(std::span<const float> a, std::span<const float> b) {
    if (a.size() != b.size() || a.empty()) return 0.0;
    double dot = 0.0, norm_a = 0.0, norm_b = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    double denom = std::sqrt(norm_a) * std::sqrt(norm_b);
    if (denom == 0.0) return 0.0;
    return dot / denom;
}

std::vector<Frame> deduplicate_frames(const std::vector<Frame>& frames, double ssim_threshold,
                                       double cosine_threshold,
                                       std::function<std::vector<float>(const Frame&)> get_embedding) {
    std::vector<Frame> result;
    std::vector<std::vector<float>> kept_embeddings;  // parallel to result, computed once per kept frame
    for (const auto& frame : frames) {
        bool is_duplicate = false;
        std::vector<float> frame_embedding;
        bool frame_embedding_ready = false;
        for (size_t k = 0; k < result.size() && !is_duplicate; ++k) {
            const auto& kept = result[k];
            if (frame.rgb_data.size() != kept.rgb_data.size() || frame.width != kept.width || frame.height != kept.height) continue;
            double ssim = compute_ssim(frame.rgb_data, kept.rgb_data, frame.width, frame.height);
            if (ssim <= ssim_threshold) continue;
            if (!get_embedding) { is_duplicate = true; break; }
            if (!frame_embedding_ready) { frame_embedding = get_embedding(frame); frame_embedding_ready = true; }
            if (kept_embeddings[k].empty()) kept_embeddings[k] = get_embedding(kept);
            if (cosine_similarity(frame_embedding, kept_embeddings[k]) > cosine_threshold) is_duplicate = true;
        }
        if (!is_duplicate) {
            result.push_back(frame);
            kept_embeddings.push_back(frame_embedding_ready ? std::move(frame_embedding) : std::vector<float>{});
        }
    }
    return result;
}

std::vector<Frame> select_frames(const std::vector<Frame>& window_frames, const SelectionConfig& config) {
    if (window_frames.empty()) return {};
    std::vector<Frame> candidates = window_frames;
    // Nothing upstream assigns Frame::score, so compute it here from the
    // configured weights: entropy (normalized to [0, 1]) and OCR density.
    for (auto& f : candidates) {
        if (f.entropy == 0.0 && has_rgb_pixels(f.rgb_data, f.width, f.height)) {
            f.entropy = compute_entropy(f.rgb_data, f.width, f.height);
        }
        if (f.score == 0.0) {
            f.score = config.entropy_weight * (f.entropy / kMaxEntropyBits) + config.ocr_weight * f.ocr_density;
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Frame& a, const Frame& b) { return a.score > b.score; });
    std::vector<Frame> selected;
    for (const auto& frame : candidates) {
        if (static_cast<int>(selected.size()) >= config.max_frames_per_window) break;
        bool too_similar = false;
        for (const auto& s : selected) {
            if (frame.width == s.width && frame.height == s.height && frame.rgb_data.size() == s.rgb_data.size() &&
                has_rgb_pixels(frame.rgb_data, frame.width, frame.height)) {
                double ssim = compute_ssim(frame.rgb_data, s.rgb_data, frame.width, frame.height);
                if (ssim > config.ssim_threshold) {
                    too_similar = true; break;
                }
            }
        }
        if (!too_similar) selected.push_back(frame);
    }
    if (static_cast<int>(selected.size()) < config.min_frames_per_window) {
        for (const auto& frame : candidates) {
            if (static_cast<int>(selected.size()) >= config.min_frames_per_window) break;
            bool exists = false;
            for (const auto& s : selected) if (s.pts_ms == frame.pts_ms) { exists = true; break; }
            if (!exists) selected.push_back(frame);
        }
    }
    std::sort(selected.begin(), selected.end(), [](const Frame& a, const Frame& b) { return a.pts_ms < b.pts_ms; });
    return selected;
}

} // namespace video2vec::vision
