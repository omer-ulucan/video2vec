#include <video2vec/version.hpp>
#include <video2vec/core/logger.hpp>
#include <video2vec/index/faiss_store.hpp>
#include <video2vec/embedding/onnx_backend.hpp>
#include <video2vec/query/query_engine.hpp>
#include <cxxopts.hpp>
#include <iostream>

using namespace video2vec;

int main(int argc, char** argv) {
    cxxopts::Options options("qa", "Interactive semantic QA over video2vec index");
    options.add_options()
        ("db", "Index path", cxxopts::value<std::string>())
        ("model", "ONNX embedding model path", cxxopts::value<std::string>())
        ("dim", "Embedding dimension", cxxopts::value<int>()->default_value("512"))
        ("topk", "Top-K results", cxxopts::value<int>()->default_value("5"))
        ("interactive", "Interactive mode", cxxopts::value<bool>()->default_value("true"))
        ("query", "Single query (non-interactive)", cxxopts::value<std::string>())
        ("h,help", "Print usage")
        ("v,version", "Print version");
    auto result = options.parse(argc, argv);
    if (result.count("help")) { std::cout << options.help() << "\n"; return 0; }
    if (result.count("version")) { std::cout << "qa " << version_string << "\n"; return 0; }
    core::Logger::initialize("qa");
    std::string db_path = result.count("db") ? result["db"].as<std::string>() : "";
    std::string model_path = result.count("model") ? result["model"].as<std::string>() : "";
    int dim = result["dim"].as<int>();
    int topk = result["topk"].as<int>();
    bool interactive = result["interactive"].as<bool>();
    if (db_path.empty()) { std::cerr << "Error: --db is required\n"; return 1; }

    auto store = std::make_shared<index::FAISSStore>();
    auto load_result = store->load(db_path);
    if (!load_result) { core::Logger::error("Failed to load index: " + load_result.error().message, {}); return 1; }

    auto embedder = std::make_shared<embedding::ONNXBackend>();
    if (!model_path.empty()) {
        embedding::EmbeddingConfig cfg{};
        cfg.visual_dim = dim; cfg.text_dim = dim;
        auto init_result = embedder->initialize(model_path, cfg);
        if (!init_result) {
            core::Logger::warn("Failed to load embedding model: " + init_result.error().message, {});
        }
    }

    auto engine = std::make_shared<query::QueryEngine>(store, embedder);

    auto process_query = [&](const std::string& query_text) {
        query::QueryRequest req{};
        req.text = query_text; req.top_k = topk; req.merge_by_time = true; req.expand_context = false;
        auto search_result = engine->search(req);
        if (!search_result) {
            std::cout << "Search failed: " << search_result.error().message << "\n";
            return;
        }
        std::cout << "\n--- Retrieved Context ---\n";
        for (const auto& r : search_result.value()) {
            std::cout << "[" << r.window_t0_ms << "ms - " << r.window_t1_ms << "ms] score=" << r.score << "\n";
            std::cout << r.text.substr(0, 300) << (r.text.size() > 300 ? "..." : "") << "\n\n";
        }
        std::cout << "--- LLM Prompt ---\n";
        std::cout << "Based on the video context above, answer the following question:\n";
        std::cout << "Question: " << query_text << "\n";
        std::cout << "Answer:\n\n";
    };

    if (!interactive || result.count("query")) {
        process_query(result["query"].as<std::string>());
        return 0;
    }

    std::cout << "qa " << version_string << " - Interactive semantic QA\n";
    std::cout << "Type your question and press Enter. Type 'quit' or 'exit' to stop.\n\n";
    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line == "quit" || line == "exit") break;
        if (line.empty()) continue;
        process_query(line);
    }
    return 0;
}
