#include <video2vec/version.hpp>
#include <video2vec/core/logger.hpp>
#include <video2vec/index/faiss_store.hpp>
#include <video2vec/embedding/onnx_backend.hpp>
#include <video2vec/query/query_engine.hpp>
#include <cxxopts.hpp>
#include <iostream>
#include <memory>
#include <string>

using namespace video2vec;

namespace {

int run(int argc, char** argv) {
    cxxopts::Options options("ask", "Query a video2vec index");
    options.add_options()
        ("d,db", "Index path (from vec2index)", cxxopts::value<std::string>())
        ("q,query", "Query text", cxxopts::value<std::string>())
        ("m,model", "ONNX text embedding model (the one used by vec2index)", cxxopts::value<std::string>())
        ("k,topk", "Top-K results", cxxopts::value<int>()->default_value("8"))
        ("merge-by-time", "Merge results by time", cxxopts::value<bool>()->default_value("true"))
        ("expand-context", "Expand context", cxxopts::value<bool>()->default_value("false"))
        ("h,help", "Print usage")
        ("v,version", "Print version");
    auto result = options.parse(argc, argv);
    if (result.count("help")) { std::cout << options.help() << "\n"; return 0; }
    if (result.count("version")) { std::cout << "ask " << version_string << "\n"; return 0; }
    core::Logger::initialize("ask");

    const std::string db_path = result.count("db") ? result["db"].as<std::string>() : "";
    const std::string query_text = result.count("query") ? result["query"].as<std::string>() : "";
    const std::string model_path = result.count("model") ? result["model"].as<std::string>() : "";
    const int topk = result["topk"].as<int>();
    if (db_path.empty() || query_text.empty() || model_path.empty()) {
        std::cerr << "Error: --db, --query and --model are required (see --help)\n";
        return 2;
    }
    if (topk <= 0) { std::cerr << "Error: --topk must be positive\n"; return 2; }

    auto store = std::make_shared<index::FAISSStore>();
    if (auto r = store->load(db_path); !r) { core::Logger::error("Failed to load index: " + r.error().message, {}); return 1; }
    auto embedder = std::make_shared<embedding::ONNXBackend>();
    embedding::EmbeddingConfig cfg{};
    cfg.storage_quant = embedding::Quantization::FP32;
    if (auto r = embedder->initialize(model_path, cfg); !r) {
        core::Logger::error("Failed to load embedding model: " + r.error().message, {});
        return 1;
    }
    query::QueryEngine engine(store, embedder);
    query::QueryRequest req{};
    req.text = query_text;
    req.top_k = topk;
    req.merge_by_time = result["merge-by-time"].as<bool>();
    req.expand_context = result["expand-context"].as<bool>();
    auto search_result = engine.search(req);
    if (!search_result) { core::Logger::error("Search failed: " + search_result.error().message, {}); return 1; }

    std::cout << "Results for: \"" << query_text << "\" (" << search_result.value().size() << " of " << store->count() << " records)\n";
    std::cout << "========================================\n";
    for (const auto& r : search_result.value()) {
        std::cout << "[" << r.window_t0_ms << " ms - " << r.window_t1_ms << " ms] score=" << r.score;
        if (!r.evidence_uris.empty()) std::cout << " evidence=" << r.evidence_uris.size();
        std::cout << "\n  " << r.text.substr(0, 200) << (r.text.size() > 200 ? "..." : "") << "\n\n";
    }
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
