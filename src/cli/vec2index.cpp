#include <video2vec/version.hpp>
#include <video2vec/core/logger.hpp>
#include <video2vec/flatbuffers/packager.hpp>
#include <video2vec/index/faiss_store.hpp>
#include <video2vec/embedding/onnx_backend.hpp>
#include <cxxopts.hpp>
#include <fstream>
#include <iostream>
#include <vector>

using namespace video2vec;

int main(int argc, char** argv) {
    cxxopts::Options options("vec2index", "Convert .vec to vector index");
    options.add_options()
        ("vec", "Input .vec file", cxxopts::value<std::string>())
        ("db", "DB type (faiss)", cxxopts::value<std::string>()->default_value("faiss"))
        ("space", "Distance metric", cxxopts::value<std::string>()->default_value("cosine"))
        ("out", "Output index path", cxxopts::value<std::string>())
        ("dim", "Embedding dimension", cxxopts::value<int>()->default_value("512"))
        ("h,help", "Print usage")
        ("v,version", "Print version");
    auto result = options.parse(argc, argv);
    if (result.count("help")) { std::cout << options.help() << "\n"; return 0; }
    if (result.count("version")) { std::cout << "vec2index " << version_string << "\n"; return 0; }
    core::Logger::initialize("vec2index");
    std::string vec_path = result.count("vec") ? result["vec"].as<std::string>() : "";
    std::string out_path = result.count("out") ? result["out"].as<std::string>() : "";
    std::string db_type = result["db"].as<std::string>();
    std::string space = result["space"].as<std::string>();
    int dim = result["dim"].as<int>();
    if (vec_path.empty() || out_path.empty()) { std::cerr << "Error: --vec and --out are required\n"; return 1; }
    core::Logger::info("Loading " + vec_path, {});
    std::ifstream file(vec_path, std::ios::binary);
    if (!file) { core::Logger::error("Cannot open: " + vec_path, {}); return 1; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto unpacked = flatbuffers::unpack_windows(data);
    if (!unpacked) { core::Logger::error("Failed to unpack: " + unpacked.error().message, {}); return 1; }
    auto store = std::make_shared<index::FAISSStore>();
    auto init_result = store->initialize(out_path, dim, space);
    if (!init_result) { core::Logger::error("Failed to initialize store: " + init_result.error().message, {}); return 1; }
    std::vector<index::VectorRecord> records;
    for (const auto& w : unpacked.value()) {
        for (const auto& word : w.words) {
            index::VectorRecord rec{};
            rec.id = "asr_" + std::to_string(w.index) + "_" + std::to_string(word.t_ms);
            rec.video_id = vec_path; rec.window_id = w.index; rec.ts_ms = word.t_ms;
            rec.type = "asr"; rec.text = word.text; rec.confidence = word.confidence;
            rec.vector.resize(dim, 0.0f); rec.vector[0] = static_cast<float>(word.t_ms % dim) / dim;
            records.push_back(std::move(rec));
        }
        for (const auto& line : w.ocr_lines) {
            index::VectorRecord rec{};
            rec.id = "ocr_" + std::to_string(w.index) + "_" + line.text.substr(0, 8);
            rec.video_id = vec_path; rec.window_id = w.index; rec.ts_ms = w.t0_ms;
            rec.type = "ocr"; rec.text = line.text; rec.confidence = line.confidence;
            rec.vector.resize(dim, 0.0f); rec.vector[1] = static_cast<float>(line.text.size() % dim) / dim;
            records.push_back(std::move(rec));
        }
    }
    auto add_result = store->add(records);
    if (!add_result) { core::Logger::error("Failed to add records: " + add_result.error().message, {}); return 1; }
    auto persist_result = store->persist(out_path);
    if (!persist_result) { core::Logger::error("Failed to persist: " + persist_result.error().message, {}); return 1; }
    core::Logger::info("Indexed " + std::to_string(records.size()) + " records to " + out_path, {});
    return 0;
}
