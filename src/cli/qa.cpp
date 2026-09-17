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
    cxxopts::Options options("qa", "Interactive semantic QA over a video2vec index");
    options.add_options()
        ("d,db", "Index path (from vec2index)", cxxopts::value<std::string>())
        ("m,model", "ONNX text embedding model (the one used by vec2index)", cxxopts::value<std::string>())
        ("k,topk", "Top-K results", cxxopts::value<int>()->default_value("5"))
        ("interactive", "Read questions from stdin", cxxopts::value<bool>()->default_value("true"))
        ("q,query", "Single question (non-interactive)", cxxopts::value<std::string>())
        ("h,help", "Print usage")
        ("v,version", "Print version");
    auto result = options.parse(argc, argv);
    if (result.count("help")) { std::cout << options.help() << "\n"; return 0; }
    if (result.count("version")) { std::cout << "qa " << version_string << "\n"; return 0; }
    core::Logger::initialize("qa");

    const std::string db_path = result.count("db") ? result["db"].as<std::string>() : "";
    const std::string model_path = result.count("model") ? result["model"].as<std::string>() : "";
    const std::string single_query = result.count("query") ? result["query"].as<std::string>() : "";
    const int topk = result["topk"].as<int>();
    const bool interactive = result["interactive"].as<bool>();
    if (db_path.empty() || model_path.empty()) { std::cerr << "Error: --db and --model are required (see --help)\n"; return 2; }
    if (topk <= 0) { std::cerr << "Error: --topk must be positive\n"; return 2; }
    if (single_query.empty() && !interactive) { std::cerr << "Error: pass --query or enable --interactive\n"; return 2; }

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

    auto process_query = [&](const std::string& query_text) -> bool {
        query::QueryRequest req{};
        req.text = query_text; req.top_k = topk; req.merge_by_time = true; req.expand_context = false;
        auto search_result = engine.search(req);
        if (!search_result) {
            std::cout << "Search failed: " << search_result.error().message << "\n";
            return false;
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
        return true;
    };

    if (!single_query.empty()) return process_query(single_query) ? 0 : 1;

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
