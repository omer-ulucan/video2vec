#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace video2vec::core {

class Counter {
public:
    void increment(uint64_t amount = 1);
    [[nodiscard]] uint64_t value() const;
private:
    mutable std::mutex mutex_;
    uint64_t value_ = 0;
};

class Gauge {
public:
    void set(double value);
    void add(double delta);
    [[nodiscard]] double value() const;
private:
    mutable std::mutex mutex_;
    double value_ = 0.0;
};

class Histogram {
public:
    void observe(double value);
    [[nodiscard]] double mean() const;
    [[nodiscard]] uint64_t count() const;
    [[nodiscard]] double percentile(double p) const;
private:
    mutable std::mutex mutex_;
    std::vector<double> values_;
};

class MetricsRegistry {
public:
    static MetricsRegistry& instance();
    Counter& counter(const std::string& name);
    Gauge& gauge(const std::string& name);
    Histogram& histogram(const std::string& name);
    std::string serialize_prometheus() const;
    void reset();
private:
    MetricsRegistry() = default;
    mutable std::mutex mutex_;
    std::map<std::string, std::unique_ptr<Counter>> counters_;
    std::map<std::string, std::unique_ptr<Gauge>> gauges_;
    std::map<std::string, std::unique_ptr<Histogram>> histograms_;
};

class Timer {
public:
    explicit Timer(const std::string& metric_name);
    ~Timer();
    [[nodiscard]] std::chrono::microseconds elapsed() const;
private:
    std::string metric_name_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace video2vec::core
