#include <video2vec/version.hpp>
#include <video2vec/core/logger.hpp>
#include <video2vec/core/config.hpp>
#include <video2vec/core/result.hpp>
#include <video2vec/ffmpeg/demuxer.hpp>
#include "ffmpeg/decoder_impl.hpp"
#include "ffmpeg/ffmpeg_helpers.hpp"
#include <video2vec/sync/timestamp.hpp>
#include <video2vec/windowing/windowing.hpp>
#include <video2vec/asr/whisper_backend.hpp>
#include <video2vec/ocr/tesseract_backend.hpp>
#include <video2vec/vision/frame_selector.hpp>
#include <video2vec/vision/patch_extractor.hpp>
#include <video2vec/embedding/onnx_backend.hpp>
#include <video2vec/flatbuffers/packager.hpp>
#include <cxxopts.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace video2vec;

namespace {

constexpr int kAsrSampleRate = 16000;

struct CliOptions {
    std::string input_path;
    std::string output_path;
    int64_t window_ms = 45000;
    int64_t overlap_ms = 5000;
    double scene_threshold = 0.25;
    std::string ocr_lang = "eng";
    int patch_size = 384;
    bool save_proof = false;
    std::string whisper_model;
    std::string embedding_model;
    std::string config_path;
    int threads = static_cast<int>(std::thread::hardware_concurrency());
    int64_t frame_interval_ms = 1000;
};

struct ParsedArgs {
    CliOptions opts;
    int exit_code = -1;  // >= 0: exit immediately with this code (help/version)
};

// Values from --config are applied first; command-line flags override them.
void apply_config(const core::Config& cfg, CliOptions& o) {
    o.window_ms = cfg.get_value<int64_t>("window_ms", o.window_ms);
    o.overlap_ms = cfg.get_value<int64_t>("overlap_ms", o.overlap_ms);
    o.scene_threshold = cfg.get_value("scene_threshold", o.scene_threshold);
    o.ocr_lang = cfg.get_value("ocr_lang", o.ocr_lang);
    o.patch_size = cfg.get_value("patch_size", o.patch_size);
    o.save_proof = cfg.get_value("save_proof", o.save_proof);
    o.whisper_model = cfg.get_value("whisper_model", o.whisper_model);
    o.embedding_model = cfg.get_value("embedding_model", o.embedding_model);
    o.threads = cfg.get_value("threads", o.threads);
    o.frame_interval_ms = cfg.get_value<int64_t>("frame_interval_ms", o.frame_interval_ms);
}

ParsedArgs parse_args(int argc, char** argv) {
    cxxopts::Options options("video2vec", "Semantic video to LLM pipeline: ASR + OCR + visual embeddings per time window");
    options.add_options()
        ("i,in", "Input video path", cxxopts::value<std::string>())
        ("o,out", "Output .vec path", cxxopts::value<std::string>())
        ("win", "Window duration in ms", cxxopts::value<int64_t>()->default_value("45000"))
        ("overlap", "Overlap between windows in ms (must be smaller than --win)", cxxopts::value<int64_t>()->default_value("5000"))
        ("scene", "Scene threshold", cxxopts::value<double>()->default_value("0.25"))
        ("ocr-lang", "Tesseract languages, empty disables OCR", cxxopts::value<std::string>()->default_value("eng"))
        ("patch", "Patch size used when the embedding model has no fixed input size", cxxopts::value<int>()->default_value("384"))
        ("save-proof", "Store the selected frames' RGB pixels in the .vec as evidence", cxxopts::value<bool>()->default_value("false"))
        ("whisper-model", "Whisper ggml model path (enables ASR)", cxxopts::value<std::string>())
        ("embedding-model", "ONNX image embedding model path (enables visual embeddings)", cxxopts::value<std::string>())
        ("frame-interval", "Sample one video frame every N ms for OCR/embeddings", cxxopts::value<int64_t>()->default_value("1000"))
        ("config", "YAML config file (keys: window_ms, overlap_ms, scene_threshold, ocr_lang, patch_size, save_proof, whisper_model, embedding_model, threads, frame_interval_ms)", cxxopts::value<std::string>())
        ("threads", "Thread count", cxxopts::value<int>())
        ("h,help", "Print usage")
        ("v,version", "Print version");
    auto result = options.parse(argc, argv);
    ParsedArgs parsed;
    if (result.count("help")) { std::cout << options.help() << "\n"; parsed.exit_code = 0; return parsed; }
    if (result.count("version")) { std::cout << "video2vec " << version_string << "\n"; parsed.exit_code = 0; return parsed; }
    CliOptions& opts = parsed.opts;
    if (result.count("config")) {
        opts.config_path = result["config"].as<std::string>();
        apply_config(core::Config::from_yaml(opts.config_path), opts);
        core::Logger::info("Loaded config: " + opts.config_path, {});
    }
    if (result.count("in")) opts.input_path = result["in"].as<std::string>();
    if (result.count("out")) opts.output_path = result["out"].as<std::string>();
    if (result.count("win")) opts.window_ms = result["win"].as<int64_t>();
    if (result.count("overlap")) opts.overlap_ms = result["overlap"].as<int64_t>();
    if (result.count("scene")) opts.scene_threshold = result["scene"].as<double>();
    if (result.count("ocr-lang")) opts.ocr_lang = result["ocr-lang"].as<std::string>();
    if (result.count("patch")) opts.patch_size = result["patch"].as<int>();
    if (result.count("save-proof")) opts.save_proof = result["save-proof"].as<bool>();
    if (result.count("whisper-model")) opts.whisper_model = result["whisper-model"].as<std::string>();
    if (result.count("embedding-model")) opts.embedding_model = result["embedding-model"].as<std::string>();
    if (result.count("frame-interval")) opts.frame_interval_ms = result["frame-interval"].as<int64_t>();
    if (result.count("threads")) opts.threads = result["threads"].as<int>();
    return parsed;
}

std::string av_error_string(int code) {
    return "AVERROR " + std::to_string(code);
}

int run(const CliOptions& opts) {
    if (opts.input_path.empty() || opts.output_path.empty()) {
        std::cerr << "Error: --in and --out are required (see --help)\n";
        return 2;
    }
    windowing::WindowingConfig win_cfg{};
    win_cfg.window_duration_ms = opts.window_ms;
    win_cfg.overlap_ms = opts.overlap_ms;
    if (auto v = windowing::validate_config(win_cfg); !v) {
        std::cerr << "Error: " << v.error().message << "\n";
        return 2;
    }
    if (opts.frame_interval_ms <= 0 || opts.patch_size <= 0) {
        std::cerr << "Error: --frame-interval and --patch must be positive\n";
        return 2;
    }

    core::Logger::info("Processing: " + opts.input_path, {});
    ffmpeg::Demuxer demuxer;
    if (int ret = demuxer.open(opts.input_path); ret < 0) {
        core::Logger::error("Failed to open video: " + opts.input_path + " (" + av_error_string(ret) + ")", {});
        return 1;
    }
    int64_t duration_ms = demuxer.duration_ms();
    if (duration_ms <= 0) {
        auto vp = demuxer.video_properties();
        if (vp.fps > 0 && vp.frame_count > 0) duration_ms = static_cast<int64_t>(vp.frame_count / vp.fps * 1000.0);
    }
    if (duration_ms <= 0) {
        core::Logger::error("Cannot determine the media duration of " + opts.input_path, {});
        return 1;
    }
    core::Logger::info("Duration: " + std::to_string(duration_ms) + " ms", {});
    auto windows = windowing::generate_windows(duration_ms, win_cfg);
    core::Logger::info("Generated " + std::to_string(windows.size()) + " windows", {});

    // ---------------------------------------------------------------- backends
    std::unique_ptr<asr::WhisperBackend> asr_backend;
    if (!opts.whisper_model.empty()) {
        asr_backend = std::make_unique<asr::WhisperBackend>();
        if (auto r = asr_backend->initialize(opts.whisper_model, "auto"); !r) {
            core::Logger::error("ASR init failed: " + r.error().message, {});
            return 1;
        }
    }
    std::unique_ptr<ocr::TesseractBackend> ocr_backend;
    if (!opts.ocr_lang.empty()) {
        ocr_backend = std::make_unique<ocr::TesseractBackend>();
        if (auto r = ocr_backend->initialize(opts.ocr_lang, ""); !r) {
            core::Logger::warn("OCR disabled: " + r.error().message, {});
            ocr_backend.reset();
        }
    }
    std::unique_ptr<embedding::ONNXBackend> embedder;
    int patch_w = opts.patch_size, patch_h = opts.patch_size;
    if (!opts.embedding_model.empty()) {
        embedder = std::make_unique<embedding::ONNXBackend>();
        embedding::EmbeddingConfig ecfg{};
        if (auto r = embedder->initialize(opts.embedding_model, ecfg); !r) {
            core::Logger::error("Embedding model init failed: " + r.error().message, {});
            return 1;
        }
        auto [mw, mh] = embedder->image_input_size();
        if (mw > 0 && mh > 0) { patch_w = mw; patch_h = mh; }
    }
    const bool want_video = ocr_backend || embedder || opts.save_proof;
    const bool want_audio = asr_backend != nullptr;

    // ---------------------------------------------------------------- decoders
    const int video_idx = demuxer.video_stream_index();
    const int audio_idx = demuxer.audio_stream_index();
    ffmpeg::Decoder video_decoder;
    ffmpeg::AudioDecoder audio_decoder;
    ffmpeg::Rational video_tb{1, 1};
    if (want_video) {
        if (video_idx < 0) {
            core::Logger::warn("No video stream: OCR/visual embeddings skipped", {});
        } else if (int ret = video_decoder.initialize(ffmpeg::native_codec_parameters(demuxer, video_idx)); ret < 0) {
            core::Logger::error("Video decoder init failed (" + av_error_string(ret) + ")", {});
            return 1;
        } else {
            video_tb = demuxer.streams()[static_cast<size_t>(video_idx)].time_base;
        }
    }
    if (want_audio) {
        if (audio_idx < 0) {
            core::Logger::warn("No audio stream: ASR skipped", {});
        } else if (int ret = audio_decoder.initialize(ffmpeg::native_codec_parameters(demuxer, audio_idx), kAsrSampleRate); ret < 0) {
            core::Logger::error("Audio decoder init failed (" + av_error_string(ret) + ")", {});
            return 1;
        }
    }

    // ---------------------------------------------------------------- per-window visual work
    struct WindowWork {
        std::vector<vision::Frame> frames;
        std::vector<ocr::OCRLine> ocr_lines;
        std::vector<embedding::Embedding> embeddings;
        int64_t processing_ms = 0;
    };
    std::vector<WindowWork> work(windows.size());
    std::deque<vision::Frame> pending;  // sampled frames not yet consumed by every window that contains them
    size_t next_window = 0;
    size_t sampled_frames = 0, selected_frames = 0;
    bool ocr_warned = false, embed_warned = false;

    vision::SelectionConfig sel_cfg{};
    sel_cfg.scene_threshold = opts.scene_threshold;
    vision::PatchConfig patch_cfg{};
    patch_cfg.target_size = opts.patch_size;

    auto finalize_window = [&](size_t wi) {
        const auto start = std::chrono::steady_clock::now();
        const auto& win = windows[wi];
        std::vector<vision::Frame> candidates;
        for (const auto& f : pending) {
            if (win.contains(f.pts_ms) || (wi + 1 == windows.size() && f.pts_ms == win.t1_ms)) candidates.push_back(f);
        }
        auto selected = vision::select_frames(candidates, sel_cfg);
        selected_frames += selected.size();
        WindowWork& out = work[wi];
        for (auto& frame : selected) {
            std::vector<ocr::OCRLine> lines;
            if (ocr_backend) {
                auto r = ocr_backend->recognize(frame.rgb_data, frame.width, frame.height, 3);
                if (r) {
                    lines = std::move(r.value().lines);
                } else if (!ocr_warned) {
                    core::Logger::warn("OCR failed: " + r.error().message, {});
                    ocr_warned = true;
                }
            }
            if (embedder) {
                auto patches = vision::extract_patches(frame, lines, patch_cfg);
                std::vector<std::vector<uint8_t>> pixels;
                for (const auto& p : patches) {
                    auto crop = vision::crop_rgb(frame.rgb_data, frame.width, frame.height, p.x, p.y, p.width, p.height);
                    auto resized = vision::resize_rgb(crop, p.width, p.height, patch_w, patch_h);
                    if (!resized.empty()) pixels.push_back(std::move(resized));
                }
                if (!pixels.empty()) {
                    auto r = embedder->encode_images(pixels, patch_w, patch_h);
                    if (r) {
                        for (auto& e : r.value()) {
                            e.type = embedding::EmbeddingType::Patch;
                            e.pts_ms = frame.pts_ms;
                            out.embeddings.push_back(std::move(e));
                        }
                    } else if (!embed_warned) {
                        core::Logger::warn("Embedding failed: " + r.error().message, {});
                        embed_warned = true;
                    }
                }
            }
            out.ocr_lines.insert(out.ocr_lines.end(), lines.begin(), lines.end());
            if (!opts.save_proof) frame.rgb_data.clear();  // keep timing/score metadata, drop pixels
            out.frames.push_back(std::move(frame));
        }
        out.processing_ms += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    };

    auto on_sampled_frame = [&](vision::Frame frame) {
        ++sampled_frames;
        pending.push_back(std::move(frame));
        while (next_window < windows.size() && pending.back().pts_ms >= windows[next_window].t1_ms) {
            finalize_window(next_window);
            ++next_window;
            if (next_window < windows.size()) {
                const int64_t keep_from = windows[next_window].t0_ms;
                while (!pending.empty() && pending.front().pts_ms < keep_from) pending.pop_front();
            }
        }
    };

    int64_t next_sample_ms = 0;
    auto handle_video_frame = [&](const ffmpeg::Frame& frame) {
        const int64_t pts = frame.pts();
        if (pts == INT64_MIN) return;  // AV_NOPTS_VALUE
        const int64_t pts_ms = sync::pts_to_ms(pts, video_tb.num, video_tb.den);
        if (pts_ms < next_sample_ms) return;
        next_sample_ms = pts_ms + opts.frame_interval_ms;
        int w = 0, h = 0;
        auto rgb = ffmpeg::frame_to_rgb(frame, w, h);
        if (rgb.empty()) return;
        vision::Frame vf{};
        vf.pts_ms = pts_ms;
        vf.width = w;
        vf.height = h;
        vf.rgb_data = std::move(rgb);
        on_sampled_frame(std::move(vf));
    };

    // ---------------------------------------------------------------- decode pass
    std::vector<float> audio;
    ffmpeg::Packet packet;
    ffmpeg::Frame frame;
    const bool decode_video = want_video && video_decoder.is_initialized();
    const bool decode_audio = want_audio && audio_decoder.is_initialized();
    while (demuxer.read_packet(packet) >= 0) {
        if (decode_video && packet.stream_index() == video_idx) {
            if (video_decoder.send_packet(packet) == 0) {
                while (video_decoder.receive_frame(frame) == 0) handle_video_frame(frame);
            }
        } else if (decode_audio && packet.stream_index() == audio_idx) {
            if (audio_decoder.send_packet(packet) == 0) {
                while (audio_decoder.receive_samples(audio) >= 0) {}
            }
        }
    }
    if (decode_video) {
        video_decoder.send_eof();
        while (video_decoder.receive_frame(frame) == 0) handle_video_frame(frame);
    }
    if (decode_audio) {
        audio_decoder.send_eof();
        while (audio_decoder.receive_samples(audio) >= 0) {}
        audio_decoder.drain_samples(audio);
    }
    while (next_window < windows.size()) {
        finalize_window(next_window);
        ++next_window;
    }
    core::Logger::info("Decoded " + std::to_string(audio.size() / kAsrSampleRate) + " s of audio, sampled " +
                       std::to_string(sampled_frames) + " frames, selected " + std::to_string(selected_frames), {});

    // ---------------------------------------------------------------- ASR per window + packaging
    std::vector<flatbuffers::PackagedWindow> packaged;
    packaged.reserve(windows.size());
    size_t total_words = 0, total_ocr = 0, total_emb = 0;
    bool asr_warned = false;
    for (size_t wi = 0; wi < windows.size(); ++wi) {
        const auto& w = windows[wi];
        flatbuffers::PackagedWindow pw{};
        pw.t0_ms = w.t0_ms; pw.t1_ms = w.t1_ms; pw.index = w.index;
        const auto start = std::chrono::steady_clock::now();
        if (asr_backend && !audio.empty()) {
            const size_t begin = std::min(audio.size(), static_cast<size_t>(w.t0_ms) * kAsrSampleRate / 1000);
            const size_t end = std::min(audio.size(), static_cast<size_t>(w.t1_ms) * kAsrSampleRate / 1000);
            if (end > begin && end - begin >= static_cast<size_t>(kAsrSampleRate)) {  // whisper needs >= 1 s
                auto r = asr_backend->transcribe(std::span<const float>(audio.data() + begin, end - begin), kAsrSampleRate);
                if (r) {
                    pw.transcript = std::move(r.value().full_transcript);
                    for (auto& seg : r.value().segments) {
                        for (auto& word : seg.words) {
                            word.t_ms += w.t0_ms;  // window-relative -> media timeline
                            pw.words.push_back(std::move(word));
                        }
                    }
                } else if (!asr_warned) {
                    core::Logger::warn("ASR failed on window " + std::to_string(w.index) + ": " + r.error().message, {});
                    asr_warned = true;
                }
            }
        }
        WindowWork& ww = work[wi];
        pw.frames = std::move(ww.frames);
        pw.ocr_lines = std::move(ww.ocr_lines);
        pw.embeddings = std::move(ww.embeddings);
        pw.processing_time_ms = ww.processing_ms +
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        total_words += pw.words.size();
        total_ocr += pw.ocr_lines.size();
        total_emb += pw.embeddings.size();
        packaged.push_back(std::move(pw));
    }

    auto buffer = flatbuffers::package_windows(packaged, version_string);
    if (!buffer) {
        core::Logger::error("Packaging failed: " + buffer.error().message, {});
        return 1;
    }
    std::ofstream out_file(opts.output_path, std::ios::binary);
    if (!out_file) {
        core::Logger::error("Cannot write output: " + opts.output_path, {});
        return 1;
    }
    out_file.write(reinterpret_cast<const char*>(buffer.value().data()), static_cast<std::streamsize>(buffer.value().size()));
    out_file.close();
    if (!out_file.good()) {
        core::Logger::error("Incomplete write to " + opts.output_path, {});
        return 1;
    }
    core::Logger::info("Wrote " + std::to_string(buffer.value().size()) + " bytes to " + opts.output_path + ": " +
                       std::to_string(packaged.size()) + " windows, " + std::to_string(total_words) + " words, " +
                       std::to_string(total_ocr) + " OCR lines, " + std::to_string(total_emb) + " embeddings", {});
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        core::Logger::initialize("video2vec-cli");
        ParsedArgs parsed = parse_args(argc, argv);
        if (parsed.exit_code >= 0) return parsed.exit_code;
        return run(parsed.opts);
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Argument error: " << e.what() << " (see --help)\n";
        return 2;
    } catch (const std::exception& e) {
        core::Logger::fatal(std::string("Unhandled exception: ") + e.what(), {});
        return 1;
    }
}
