#include <video2vec/version.hpp>
#include <video2vec/core/logger.hpp>
#include <video2vec/core/config.hpp>
#include <video2vec/core/result.hpp>
#include <video2vec/ffmpeg/demuxer.hpp>
#include <video2vec/ffmpeg/decoder.hpp>
#include <video2vec/sync/synchronizer.hpp>
#include <video2vec/windowing/windowing.hpp>
#include <video2vec/asr/whisper_backend.hpp>
#include <video2vec/ocr/tesseract_backend.hpp>
#include <video2vec/vision/frame_selector.hpp>
#include <video2vec/vision/patch_extractor.hpp>
#include <video2vec/embedding/onnx_backend.hpp>
#include <video2vec/flatbuffers/packager.hpp>
#include <cxxopts.hpp>
#include <fstream>
#include <iostream>
#include <thread>

using namespace video2vec;

struct CliOptions {
    std::string input_path;
    std::string output_path;
    int window_ms = 45000;
    int overlap_ms = 5000;
    double scene_threshold = 0.25;
    std::string ocr_lang = "eng+tur";
    int patch_size = 384;
    bool save_proof = false;
    std::string whisper_model;
    std::string embedding_model;
    std::string config_path;
    int threads = static_cast<int>(std::thread::hardware_concurrency());
};

CliOptions parse_args(int argc, char** argv) {
    cxxopts::Options options("video2vec", "Semantic video to LLM pipeline");
    options.add_options()
        ("i,in", "Input video path", cxxopts::value<std::string>())
        ("o,out", "Output .vec path", cxxopts::value<std::string>())
        ("win", "Window duration in ms", cxxopts::value<int>()->default_value("45000"))
        ("overlap", "Overlap in ms", cxxopts::value<int>()->default_value("5000"))
        ("scene", "Scene threshold", cxxopts::value<double>()->default_value("0.25"))
        ("ocr-lang", "OCR languages", cxxopts::value<std::string>()->default_value("eng+tur"))
        ("patch", "Patch size", cxxopts::value<int>()->default_value("384"))
        ("save-proof", "Save PNG evidence", cxxopts::value<bool>()->default_value("false"))
        ("whisper-model", "Whisper model path", cxxopts::value<std::string>())
        ("embedding-model", "ONNX embedding model path", cxxopts::value<std::string>())
        ("config", "Config file path", cxxopts::value<std::string>())
        ("threads", "Thread count", cxxopts::value<int>())
        ("h,help", "Print usage")
        ("v,version", "Print version");
    auto result = options.parse(argc, argv);
    if (result.count("help")) { std::cout << options.help() << "\n"; std::exit(0); }
    if (result.count("version")) { std::cout << "video2vec " << version_string << "\n"; std::exit(0); }
    CliOptions opts;
    if (result.count("in")) opts.input_path = result["in"].as<std::string>();
    if (result.count("out")) opts.output_path = result["out"].as<std::string>();
    if (result.count("win")) opts.window_ms = result["win"].as<int>();
    if (result.count("overlap")) opts.overlap_ms = result["overlap"].as<int>();
    if (result.count("scene")) opts.scene_threshold = result["scene"].as<double>();
    if (result.count("ocr-lang")) opts.ocr_lang = result["ocr-lang"].as<std::string>();
    if (result.count("patch")) opts.patch_size = result["patch"].as<int>();
    if (result.count("save-proof")) opts.save_proof = result["save-proof"].as<bool>();
    if (result.count("whisper-model")) opts.whisper_model = result["whisper-model"].as<std::string>();
    if (result.count("embedding-model")) opts.embedding_model = result["embedding-model"].as<std::string>();
    if (result.count("config")) opts.config_path = result["config"].as<std::string>();
    if (result.count("threads")) opts.threads = result["threads"].as<int>();
    return opts;
}

int main(int argc, char** argv) {
    try {
        core::Logger::initialize("video2vec-cli");
        auto opts = parse_args(argc, argv);
        if (opts.input_path.empty() || opts.output_path.empty()) {
            std::cerr << "Error: --in and --out are required\n";
            return 1;
        }
        core::Logger::info("Processing: " + opts.input_path, {});
        if (!opts.config_path.empty()) {
            core::Config cfg = core::Config::from_yaml(opts.config_path);
            core::Logger::info("Loaded config: " + opts.config_path, {});
        }
        ffmpeg::Demuxer demuxer;
        int ret = demuxer.open(opts.input_path);
        if (ret < 0) {
            core::Logger::error("Failed to open video: " + opts.input_path, {});
            return 1;
        }
        auto video_props = demuxer.video_properties();
        auto audio_props = demuxer.audio_properties();
        int64_t duration_ms = static_cast<int64_t>(video_props.fps > 0 && video_props.frame_count > 0
            ? (video_props.frame_count / video_props.fps * 1000.0) : 0);
        core::Logger::info("Duration: " + std::to_string(duration_ms) + " ms", {});
        windowing::WindowingConfig win_cfg{};
        win_cfg.window_duration_ms = opts.window_ms;
        win_cfg.overlap_ms = opts.overlap_ms;
        auto windows = windowing::generate_windows(duration_ms, win_cfg);
        core::Logger::info("Generated " + std::to_string(windows.size()) + " windows", {});
        std::vector<flatbuffers::PackagedWindow> packaged;
        for (const auto& w : windows) {
            flatbuffers::PackagedWindow pw{};
            pw.t0_ms = w.t0_ms; pw.t1_ms = w.t1_ms; pw.index = w.index;
            pw.transcript = "[placeholder ASR for window " + std::to_string(w.index) + "]";
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
        out_file.write(reinterpret_cast<const char*>(buffer.value().data()), buffer.value().size());
        out_file.close();
        core::Logger::info("Wrote " + std::to_string(buffer.value().size()) + " bytes to " + opts.output_path, {});
        core::Logger::info("Done.", {});
        return 0;
    } catch (const std::exception& e) {
        core::Logger::fatal(std::string("Unhandled exception: ") + e.what(), {});
        return 1;
    }
}
