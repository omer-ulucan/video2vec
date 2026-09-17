#pragma once

#include "video2vec/vision/frame.hpp"
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace video2vec::vision {

struct SelectionConfig {
    int max_frames_per_window = 6;
    int min_frames_per_window = 4;
    double scene_threshold = 0.25;
    double entropy_weight = 1.0;
    double ocr_weight = 1.0;
    double ssim_threshold = 0.99;
    double cosine_threshold = 0.995;
};

// Ranks the window's frames and returns up to max_frames_per_window of them
// in timestamp order, dropping near-duplicates (SSIM above ssim_threshold).
// Frames with score == 0 are scored as
//   entropy_weight * entropy / 8 + ocr_weight * ocr_density,
// computing entropy from rgb_data when it is unset; the returned frames
// carry the computed entropy and score.
std::vector<Frame> select_frames(const std::vector<Frame>& window_frames, const SelectionConfig& config);
double compute_entropy(const std::vector<uint8_t>& rgb_data, int width, int height);
double compute_ssim(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, int width, int height);
double cosine_similarity(std::span<const float> a, std::span<const float> b);
std::vector<Frame> deduplicate_frames(const std::vector<Frame>& frames, double ssim_threshold,
                                       double cosine_threshold,
                                       std::function<std::vector<float>(const Frame&)> get_embedding);

} // namespace video2vec::vision
