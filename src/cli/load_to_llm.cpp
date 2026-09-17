#include <video2vec/version.hpp>
#include <video2vec/core/logger.hpp>
#include <video2vec/flatbuffers/packager.hpp>
#include <cxxopts.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>

using namespace video2vec;
using json = nlohmann::json;

namespace {

int run(int argc, char** argv) {
    cxxopts::Options options("load-to-llm", "Convert a .vec file to LLM-ready context");
    options.add_options()
        ("i,vec", "Input .vec file", cxxopts::value<std::string>())
        ("f,format", "Output format: json, markdown or text", cxxopts::value<std::string>()->default_value("markdown"))
        ("o,out", "Output file (default stdout)", cxxopts::value<std::string>())
        ("max-chars", "Max characters per transcript entry", cxxopts::value<int>()->default_value("2000"))
        ("h,help", "Print usage")
        ("v,version", "Print version");
    auto result = options.parse(argc, argv);
    if (result.count("help")) { std::cout << options.help() << "\n"; return 0; }
    if (result.count("version")) { std::cout << "load-to-llm " << version_string << "\n"; return 0; }
    core::Logger::initialize("load-to-llm");

    const std::string vec_path = result.count("vec") ? result["vec"].as<std::string>() : "";
    const std::string format = result["format"].as<std::string>();
    const std::string out_path = result.count("out") ? result["out"].as<std::string>() : "";
    const int max_chars = result["max-chars"].as<int>();
    if (vec_path.empty()) { std::cerr << "Error: --vec is required (see --help)\n"; return 2; }
    if (format != "json" && format != "markdown" && format != "text") {
        std::cerr << "Error: --format must be json, markdown or text (got '" << format << "')\n";
        return 2;
    }
    if (max_chars <= 0) { std::cerr << "Error: --max-chars must be positive\n"; return 2; }

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

    const auto clip = [max_chars](const std::string& s) { return s.substr(0, static_cast<size_t>(max_chars)); };
    if (format == "json") {
        json j = json::array();
        for (const auto& w : unpacked.value()) {
            json entry;
            entry["index"] = w.index;
            entry["t0_ms"] = w.t0_ms;
            entry["t1_ms"] = w.t1_ms;
            entry["transcript"] = clip(w.transcript);
            json words = json::array();
            for (const auto& word : w.words) words.push_back({{"t_ms", word.t_ms}, {"dt_ms", word.dt_ms}, {"text", word.text}, {"confidence", word.confidence}});
            entry["words"] = words;
            json ocr = json::array();
            for (const auto& line : w.ocr_lines) {
                ocr.push_back({{"text", line.text}, {"confidence", line.confidence},
                               {"bbox", {line.bbox.x, line.bbox.y, line.bbox.w, line.bbox.h}}, {"pii_flagged", line.pii_flagged}});
            }
            entry["ocr"] = ocr;
            entry["frames"] = w.frames.size();
            entry["embeddings"] = w.embeddings.size();
            j.push_back(entry);
        }
        *out << j.dump(2) << "\n";
    } else if (format == "markdown") {
        *out << "# Video Context\n\n";
        for (const auto& w : unpacked.value()) {
            *out << "## Window " << w.index << " [" << w.t0_ms << "ms - " << w.t1_ms << "ms]\n\n";
            *out << clip(w.transcript) << "\n\n";
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
            *out << "[" << w.t0_ms << "ms-" << w.t1_ms << "ms] " << clip(w.transcript) << "\n";
        }
    }
    out->flush();
    if (!out->good()) { core::Logger::error("Failed to write output" + (out_path.empty() ? std::string() : " to " + out_path), {}); return 1; }
    core::Logger::info("Wrote " + std::to_string(unpacked.value().size()) + " windows", {});
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Argument error: " << e.what() << " (see --help)\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
}
