#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace video2vec::core {

struct ProgressUpdate {
    std::string stage;
    uint64_t current;
    uint64_t total;
    double percent() const {
        if (total == 0) return 0.0;
        return static_cast<double>(current) / static_cast<double>(total) * 100.0;
    }
};

class IProgressReporter {
public:
    virtual ~IProgressReporter() = default;
    virtual void report(const ProgressUpdate& update) = 0;
    virtual void log(const std::string& message) = 0;
};

class NullProgressReporter : public IProgressReporter {
public:
    void report(const ProgressUpdate&) override {}
    void log(const std::string&) override {}
};

class CallbackProgressReporter : public IProgressReporter {
public:
    explicit CallbackProgressReporter(
        std::function<void(const ProgressUpdate&)> on_progress,
        std::function<void(const std::string&)> on_log = nullptr)
        : on_progress_(std::move(on_progress)), on_log_(std::move(on_log)) {}
    void report(const ProgressUpdate& update) override {
        if (on_progress_) on_progress_(update);
    }
    void log(const std::string& message) override {
        if (on_log_) on_log_(message);
    }
private:
    std::function<void(const ProgressUpdate&)> on_progress_;
    std::function<void(const std::string&)> on_log_;
};

} // namespace video2vec::core
