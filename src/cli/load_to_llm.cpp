#include <video2vec/version.hpp>
#include <video2vec/core/logger.hpp>
#include <video2vec/flatbuffers/packager.hpp>
#include <cxxopts.hpp>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace video2vec;
using json = nlohmann::json;

int main(int argc, char** argv) {
    cxxopts::Options options("load-to-llm", "Convert .vec to LLM-ready context");
    options.add_options()
        ("vec", "Input .vec file", cxxopts::value<std::string>())
        ("format", "Output format (json|markdown|text)", cxxopts::value<std::string>()->default_value("markdown"))
        ("out", "Output file (default stdout)", cxxopts::value<std::string>())
        ("max-chars", "Max characters per transcript entry", cxxopts::value<int>()->default_value("2000"))
        ("h,help", "Print usage")
        ("v,version", "Print version");
    auto result = options.parse(argc, argv);
    if (result.count("help")) { std::cout << options.help() << "\n"; return 0; }
    if (result.count("version")) { std::cout << "load-to-llm " << version_string << "\n"; return 0; }
    core::Logger::initialize("load-to-llm");
    std::string vec_path = result.count("vec") ? result["vec"].as<std::string>() : "";
    std::string format = result["format"].as<std::string>();
    std::string out_path = result.count("out") ? result["out"].as<std::string>() : "";
    int max_chars = result["max-chars"].as<int>();
    if (vec_path.empty()) { std::cerr << "Error: --vec is required\n"; return 1; }

    std::ifstream file(vec_path, std::ios::binary);
    if (!file) { core::Logger::error("Cannot open: " + vec_path, {}); return 1; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto unpacked = flatbuffers::unpack_windows(data);
    if (!unpacked) { core::Logger::error("Failed to unpack: " + unpacked.error().message, {}); return 1; }

    std::ostream* out = &std::cout;
    std::ofstream out_file;
    if (!out_path.empty()) {
        out_file.open(out_path);
        if (!out_file) { core::Logger::error("Cannot write: " + out_path, {}); return 1; }
        out = &out_file;
    }

    if (format == "json") {
        json j = json::array();
        for (const auto& w : unpacked.value()) {
            json entry;
            entry["index"] = w.index;
            entry["t0_ms"] = w.t0_ms;
            entry["t1_ms"] = w.t1_ms;
            entry["transcript"] = w.transcript.substr(0, max_chars);
            j.push_back(entry);
        }
        *out << j.dump(2) << "\n";
    } else if (format == "markdown") {
        *out << "# Video Context\n\n";
        for (const auto& w : unpacked.value()) {
            *out << "## Window " << w.index << " [" << w.t0_ms << "ms - " << w.t1_ms << "ms]\n\n";
            *out << w.transcript.substr(0, max_chars) << "\n\n";
            if (!w.ocr_lines.empty()) {
                *out << "**OCR:**\n";
                for (const auto& line : w.ocr_lines) {
                    *out << "- " << line.text << " (conf: " << static_cast<int>(line.confidence * 100) << "%)\n";
                }
                *out << "\n";
            }
        }
    } else {
        for (const auto& w : unpacked.value()) {
            *out << "[" << w.t0_ms << "ms-" << w.t1_ms << "ms] "
                 << w.transcript.substr(0, max_chars) << "\n";
        }
    }

    core::Logger::info("Wrote " + std::to_string(unpacked.value().size()) + " windows", {});
    return 0;
}
