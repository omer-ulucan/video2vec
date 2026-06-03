#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <cstdint>

namespace video2vec::core {

enum class LogLevel : uint8_t {
    Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Fatal = 5,
};

struct LogContext {
    std::string module;
    std::string file;
    int line = 0;
    std::string function;
    std::string request_id;
};

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(LogLevel level, const std::string& message, const LogContext& ctx) = 0;
    virtual void flush() = 0;
    virtual void set_level(LogLevel level) = 0;
};

class Logger {
public:
    static void initialize(const std::string& name = "video2vec",
                          const std::string& pattern = "[%Y-%m-%d %H:%M:%S.%e] [%t] [%l] %v");
    static void initialize_async(size_t queue_size = 8192, const std::string& name = "video2vec");
    static ILogger& instance();
    static void set_level(LogLevel level);
    static void flush();
    static void trace(const std::string& msg, const LogContext& ctx = {});
    static void debug(const std::string& msg, const LogContext& ctx = {});
    static void info(const std::string& msg, const LogContext& ctx = {});
    static void warn(const std::string& msg, const LogContext& ctx = {});
    static void error(const std::string& msg, const LogContext& ctx = {});
    static void fatal(const std::string& msg, const LogContext& ctx = {});
private:
    static std::atomic<ILogger*> logger_ptr_;
    static std::unique_ptr<ILogger> logger_owner_;
    static std::once_flag init_flag_;
};

} // namespace video2vec::core
