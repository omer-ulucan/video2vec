#include <video2vec/version.hpp>
#include <video2vec/core/logger.hpp>
#include <video2vec/flatbuffers/packager.hpp>
#include <video2vec/index/faiss_store.hpp>
#include <video2vec/embedding/onnx_backend.hpp>
#include <cxxopts.hpp>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace video2vec;

namespace {

int run(int argc, char** argv) {
    cxxopts::Options options("vec2index", "Build a vector index from a .vec file");
    options.add_options()
        ("i,vec", "Input .vec file", cxxopts::value<std::string>())
        ("o,out", "Output index path", cxxopts::value<std::string>())
        ("m,model", "ONNX text embedding model (float [1,dim] input) used to embed transcript words and OCR lines; without it only visual embeddings are indexed", cxxopts::value<std::string>())
        ("space", "Distance metric: cosine, inner_product or l2", cxxopts::value<std::string>()->default_value("cosine"))
        ("dim", "Embedding dimension of the index (must match the embeddings)", cxxopts::value<int>()->default_value("512"))
        ("db", "Index backend (only faiss is available)", cxxopts::value<std::string>()->default_value("faiss"))
        ("h,help", "Print usage")
        ("v,version", "Print version");
    auto result = options.parse(argc, argv);
    if (result.count("help")) { std::cout << options.help() << "\n"; return 0; }
    if (result.count("version")) { std::cout << "vec2index " << version_string << "\n"; return 0; }
    core::Logger::initialize("vec2index");

    const std::string vec_path = result.count("vec") ? result["vec"].as<std::string>() : "";
    const std::string out_path = result.count("out") ? result["out"].as<std::string>() : "";
    const std::string model_path = result.count("model") ? result["model"].as<std::string>() : "";
    const std::string space = result["space"].as<std::string>();
    const std::string db_type = result["db"].as<std::string>();
    const int dim = result["dim"].as<int>();
    if (vec_path.empty() || out_path.empty()) { std::cerr << "Error: --vec and --out are required (see --help)\n"; return 2; }
    if (dim <= 0) { std::cerr << "Error: --dim must be positive\n"; return 2; }
    if (db_type != "faiss") { std::cerr << "Error: unsupported --db '" << db_type << "' (only faiss)\n"; return 2; }
    if (space != "cosine" && space != "inner_product" && space != "l2") {
        std::cerr << "Error: --space must be cosine, inner_product or l2\n";
        return 2;
    }

    core::Logger::info("Loading " + vec_path, {});
    std::ifstream file(vec_path, std::ios::binary);
    if (!file) { core::Logger::error("Cannot open: " + vec_path, {}); return 1; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto unpacked = flatbuffers::unpack_windows(data);
    if (!unpacked) { core::Logger::error("Failed to unpack: " + unpacked.error().message, {}); return 1; }

    std::unique_ptr<embedding::ONNXBackend> text_encoder;
    if (!model_path.empty()) {
        text_encoder = std::make_unique<embedding::ONNXBackend>();
        embedding::EmbeddingConfig cfg{};
        cfg.text_dim = dim;
        cfg.storage_quant = embedding::Quantization::FP32;
        if (auto r = text_encoder->initialize(model_path, cfg); !r) {
            core::Logger::error("Failed to load text model: " + r.error().message, {});
            return 1;
        }
    }

    // Embeds a batch of texts into records of the given type.
    auto embed_texts = [&](const std::vector<std::string>& texts, std::vector<std::vector<float>>& out) -> bool {
        out.clear();
        if (texts.empty()) return true;
        auto r = text_encoder->encode_text(texts);
        if (!r) { core::Logger::error("Text embedding failed: " + r.error().message, {}); return false; }
        for (const auto& e : r.value()) out.push_back(embedding::to_float(e));
        return true;
    };

    std::vector<index::VectorRecord> records;
    size_t n_asr = 0, n_ocr = 0, n_visual = 0, skipped_text = 0;
    for (const auto& w : unpacked.value()) {
        for (size_t n = 0; n < w.embeddings.size(); ++n) {
            index::VectorRecord rec{};
            rec.vector = embedding::to_float(w.embeddings[n]);
            if (rec.vector.size() != static_cast<size_t>(dim)) {
                core::Logger::error("Visual embedding in window " + std::to_string(w.index) + " has dimension " +
                                    std::to_string(rec.vector.size()) + ", expected --dim " + std::to_string(dim), {});
                return 1;
            }
            rec.id = "vis_" + std::to_string(w.index) + "_" + std::to_string(n);
            rec.video_id = vec_path; rec.window_id = w.index; rec.ts_ms = w.embeddings[n].pts_ms;
            rec.type = "visual";
            records.push_back(std::move(rec));
            ++n_visual;
        }
        if (!text_encoder) {
            skipped_text += w.words.size() + w.ocr_lines.size();
            continue;
        }
        std::vector<std::string> texts;
        std::vector<std::vector<float>> vectors;
        for (const auto& word : w.words) texts.push_back(word.text);
        if (!embed_texts(texts, vectors)) return 1;
        for (size_t n = 0; n < w.words.size(); ++n) {
            const auto& word = w.words[n];
            if (vectors[n].size() != static_cast<size_t>(dim)) {
                core::Logger::error("Text model produced dimension " + std::to_string(vectors[n].size()) + ", expected --dim " + std::to_string(dim), {});
                return 1;
            }
            index::VectorRecord rec{};
            rec.id = "asr_" + std::to_string(w.index) + "_" + std::to_string(n);
            rec.video_id = vec_path; rec.window_id = w.index; rec.ts_ms = word.t_ms;
            rec.type = "asr"; rec.text = word.text; rec.confidence = word.confidence;
            rec.vector = std::move(vectors[n]);
            records.push_back(std::move(rec));
            ++n_asr;
        }
        texts.clear();
        for (const auto& line : w.ocr_lines) texts.push_back(line.text);
        if (!embed_texts(texts, vectors)) return 1;
        for (size_t n = 0; n < w.ocr_lines.size(); ++n) {
            const auto& line = w.ocr_lines[n];
            if (vectors[n].size() != static_cast<size_t>(dim)) {
                core::Logger::error("Text model produced dimension " + std::to_string(vectors[n].size()) + ", expected --dim " + std::to_string(dim), {});
                return 1;
            }
            index::VectorRecord rec{};
            rec.id = "ocr_" + std::to_string(w.index) + "_" + std::to_string(n);
            rec.video_id = vec_path; rec.window_id = w.index; rec.ts_ms = w.t0_ms;
            rec.type = "ocr"; rec.text = line.text; rec.confidence = line.confidence;
            rec.bbox = {static_cast<float>(line.bbox.x), static_cast<float>(line.bbox.y),
                        static_cast<float>(line.bbox.w), static_cast<float>(line.bbox.h)};
            rec.vector = std::move(vectors[n]);
            records.push_back(std::move(rec));
            ++n_ocr;
        }
    }
    if (skipped_text > 0) {
        core::Logger::warn(std::to_string(skipped_text) + " transcript words / OCR lines were not indexed: pass --model to embed text", {});
    }
    if (records.empty()) {
        core::Logger::error("Nothing to index: the .vec has no embeddings" + std::string(text_encoder ? " or text" : ", and no --model was given for text"), {});
        return 1;
    }

    auto store = std::make_shared<index::FAISSStore>();
    if (auto r = store->initialize(out_path, dim, space); !r) { core::Logger::error("Failed to initialize store: " + r.error().message, {}); return 1; }
    if (auto r = store->add(records); !r) { core::Logger::error("Failed to add records: " + r.error().message, {}); return 1; }
    if (auto r = store->persist(out_path); !r) { core::Logger::error("Failed to persist: " + r.error().message, {}); return 1; }
    core::Logger::info("Indexed " + std::to_string(records.size()) + " records (" + std::to_string(n_asr) + " asr, " +
                       std::to_string(n_ocr) + " ocr, " + std::to_string(n_visual) + " visual) to " + out_path, {});
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
