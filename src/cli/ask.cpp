#include <video2vec/version.hpp>
#include <video2vec/core/logger.hpp>
#include <video2vec/index/faiss_store.hpp>
#include <video2vec/embedding/onnx_backend.hpp>
#include <video2vec/query/query_engine.hpp>
#include <cxxopts.hpp>
#include <iostream>

using namespace video2vec;

int main(int argc, char** argv) {
    cxxopts::Options options("ask", "Query video2vec index");
    options.add_options()
        ("db", "Index path", cxxopts::value<std::string>())
        ("q", "Query text", cxxopts::value<std::string>())
        ("topk", "Top-K results", cxxopts::value<int>()->default_value("8"))
        ("merge-by-time", "Merge results by time", cxxopts::value<bool>()->default_value("true"))
        ("expand-context", "Expand context", cxxopts::value<bool>()->default_value("false"))
        ("dim", "Embedding dimension", cxxopts::value<int>()->default_value("512"))
        ("h,help", "Print usage")
        ("v,version", "Print version");
    auto result = options.parse(argc, argv);
    if (result.count("help")) { std::cout << options.help() << "\n"; return 0; }
    if (result.count("version")) { std::cout << "ask " << version_string << "\n"; return 0; }
    core::Logger::initialize("ask");
    std::string db_path = result.count("db") ? result["db"].as<std::string>() : "";
    std::string query_text = result.count("q") ? result["q"].as<std::string>() : "";
    int topk = result["topk"].as<int>();
    bool merge_by_time = result["merge-by-time"].as<bool>();
    bool expand_context = result["expand-context"].as<bool>();
    int dim = result["dim"].as<int>();
    if (db_path.empty() || query_text.empty()) { std::cerr << "Error: --db and --q are required\n"; return 1; }
    auto store = std::make_shared<index::FAISSStore>();
    auto load_result = store->load(db_path);
    if (!load_result) { core::Logger::error("Failed to load index: " + load_result.error().message, {}); return 1; }
    auto embedder = std::make_shared<embedding::ONNXBackend>();
    auto engine = std::make_shared<query::QueryEngine>(store, embedder);
    query::QueryRequest req{};
    req.text = query_text; req.top_k = topk; req.merge_by_time = merge_by_time; req.expand_context = expand_context;
    auto search_result = engine->search(req);
    if (!search_result) { core::Logger::error("Search failed: " + search_result.error().message, {}); return 1; }
    std::cout << "Results for: \"" << query_text << "\"\n";
    std::cout << "========================================\n";
    for (const auto& r : search_result.value()) {
        std::cout << "[" << r.window_t0_ms << " ms - " << r.window_t1_ms << " ms] score=" << r.score << "\n";
        std::cout << "  " << r.text.substr(0, 200) << (r.text.size() > 200 ? "..." : "") << "\n\n";
    }
    return 0;
}
