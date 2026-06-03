#include "video2vec/core/logger.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <mutex>

namespace video2vec::core {

namespace {
    class SpdlogLogger : public ILogger {
    public:
        explicit SpdlogLogger(std::shared_ptr<spdlog::logger> logger)
            : logger_(std::move(logger)) {}
        void log(LogLevel level, const std::string& message, const LogContext& ctx) override {
            auto spd_level = to_spdlog(level);
            if (ctx.module.empty()) {
                logger_->log(spd_level, "{}", message);
            } else {
                logger_->log(spd_level, "[{}] {}", ctx.module, message);
            }
        }
        void flush() override { logger_->flush(); }
        void set_level(LogLevel level) override { logger_->set_level(to_spdlog(level)); }
    private:
        static spdlog::level::level_enum to_spdlog(LogLevel level) {
            switch (level) {
            case LogLevel::Trace: return spdlog::level::trace;
            case LogLevel::Debug: return spdlog::level::debug;
            case LogLevel::Info: return spdlog::level::info;
            case LogLevel::Warn: return spdlog::level::warn;
            case LogLevel::Error: return spdlog::level::err;
            case LogLevel::Fatal: return spdlog::level::critical;
            default: return spdlog::level::info;
            }
        }
        std::shared_ptr<spdlog::logger> logger_;
    };
}

std::atomic<ILogger*> Logger::logger_ptr_{nullptr};
std::unique_ptr<ILogger> Logger::logger_owner_;
std::once_flag Logger::init_flag_;

void Logger::initialize(const std::string& name, const std::string& pattern) {
    std::call_once(init_flag_, [&]() {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern(pattern);
        auto logger = std::make_shared<spdlog::logger>(name, console_sink);
        logger->set_level(spdlog::level::info);
        logger_owner_ = std::make_unique<SpdlogLogger>(logger);
        logger_ptr_.store(logger_owner_.get(), std::memory_order_release);
    });
}

void Logger::initialize_async(size_t queue_size, const std::string& name) {
    std::call_once(init_flag_, [&]() {
        spdlog::init_thread_pool(queue_size, 1);
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto logger = std::make_shared<spdlog::async_logger>(
            name, console_sink, spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        logger_owner_ = std::make_unique<SpdlogLogger>(logger);
        logger_ptr_.store(logger_owner_.get(), std::memory_order_release);
    });
}

ILogger& Logger::instance() {
    ILogger* ptr = logger_ptr_.load(std::memory_order_acquire);
    if (ptr) return *ptr;
    initialize();
    ptr = logger_ptr_.load(std::memory_order_acquire);
    return *ptr;
}

void Logger::set_level(LogLevel level) { instance().set_level(level); }
void Logger::flush() { instance().flush(); }
void Logger::trace(const std::string& msg, const LogContext& ctx) { instance().log(LogLevel::Trace, msg, ctx); }
void Logger::debug(const std::string& msg, const LogContext& ctx) { instance().log(LogLevel::Debug, msg, ctx); }
void Logger::info(const std::string& msg, const LogContext& ctx) { instance().log(LogLevel::Info, msg, ctx); }
void Logger::warn(const std::string& msg, const LogContext& ctx) { instance().log(LogLevel::Warn, msg, ctx); }
void Logger::error(const std::string& msg, const LogContext& ctx) { instance().log(LogLevel::Error, msg, ctx); }
void Logger::fatal(const std::string& msg, const LogContext& ctx) { instance().log(LogLevel::Fatal, msg, ctx); }

} // namespace video2vec::core
