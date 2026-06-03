#include "video2vec/core/metrics.hpp"

#include <algorithm>
#include <numeric>
#include <sstream>

namespace video2vec::core {

void Counter::increment(uint64_t amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    value_ += amount;
}

uint64_t Counter::value() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return value_;
}

void Gauge::set(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    value_ = value;
}

void Gauge::add(double delta) {
    std::lock_guard<std::mutex> lock(mutex_);
    value_ += delta;
}

double Gauge::value() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return value_;
}

void Histogram::observe(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    values_.push_back(value);
}

double Histogram::mean() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (values_.empty()) return 0.0;
    return std::accumulate(values_.begin(), values_.end(), 0.0) / values_.size();
}

uint64_t Histogram::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return values_.size();
}

double Histogram::percentile(double p) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (values_.empty()) return 0.0;
    std::vector<double> sorted = values_;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(p * sorted.size());
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx];
}

MetricsRegistry& MetricsRegistry::instance() {
    static MetricsRegistry instance;
    return instance;
}

Counter& MetricsRegistry::counter(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = counters_.find(name);
    if (it == counters_.end()) {
        auto [inserted, _] = counters_.emplace(name, std::make_unique<Counter>());
        it = inserted;
    }
    return *it->second;
}

Gauge& MetricsRegistry::gauge(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = gauges_.find(name);
    if (it == gauges_.end()) {
        auto [inserted, _] = gauges_.emplace(name, std::make_unique<Gauge>());
        it = inserted;
    }
    return *it->second;
}

Histogram& MetricsRegistry::histogram(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = histograms_.find(name);
    if (it == histograms_.end()) {
        auto [inserted, _] = histograms_.emplace(name, std::make_unique<Histogram>());
        it = inserted;
    }
    return *it->second;
}

std::string MetricsRegistry::serialize_prometheus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream out;
    for (const auto& [name, counter] : counters_) {
        out << "# TYPE " << name << " counter\n";
        out << name << " " << counter->value() << "\n";
    }
    for (const auto& [name, gauge] : gauges_) {
        out << "# TYPE " << name << " gauge\n";
        out << name << " " << gauge->value() << "\n";
    }
    for (const auto& [name, hist] : histograms_) {
        out << "# TYPE " << name << " histogram\n";
        out << name << "_count " << hist->count() << "\n";
        out << name << "_sum " << hist->mean() * hist->count() << "\n";
    }
    return out.str();
}

void MetricsRegistry::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_.clear();
    gauges_.clear();
    histograms_.clear();
}

Timer::Timer(const std::string& metric_name)
    : metric_name_(metric_name), start_(std::chrono::steady_clock::now()) {}

Timer::~Timer() {
    auto elapsed = std::chrono::steady_clock::now() - start_;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    MetricsRegistry::instance().histogram(metric_name_ + "_duration_us").observe(static_cast<double>(us));
}

std::chrono::microseconds Timer::elapsed() const {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start_);
}

} // namespace video2vec::core
