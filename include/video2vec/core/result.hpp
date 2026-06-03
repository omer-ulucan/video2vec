#pragma once

#include <string>
#include <variant>
#include <system_error>
#include <type_traits>
#include <utility>

namespace video2vec::core {

enum class ErrorCode : int {
    Success = 0,
    InvalidArgument = 1,
    NotFound = 2,
    AlreadyExists = 3,
    IoError = 4,
    DecodeError = 5,
    EncodingError = 6,
    ModelError = 7,
    OutOfMemory = 8,
    Timeout = 9,
    Cancelled = 10,
    Unsupported = 11,
    InternalError = 12,
    ConfigError = 13,
    PluginError = 14,
    SyncError = 15,
};

class ErrorCategory : public std::error_category {
public:
    const char* name() const noexcept override { return "video2vec"; }
    std::string message(int ev) const override {
        switch (static_cast<ErrorCode>(ev)) {
        case ErrorCode::Success: return "success";
        case ErrorCode::InvalidArgument: return "invalid argument";
        case ErrorCode::NotFound: return "not found";
        case ErrorCode::AlreadyExists: return "already exists";
        case ErrorCode::IoError: return "I/O error";
        case ErrorCode::DecodeError: return "decode error";
        case ErrorCode::EncodingError: return "encoding error";
        case ErrorCode::ModelError: return "model error";
        case ErrorCode::OutOfMemory: return "out of memory";
        case ErrorCode::Timeout: return "timeout";
        case ErrorCode::Cancelled: return "cancelled";
        case ErrorCode::Unsupported: return "unsupported";
        case ErrorCode::InternalError: return "internal error";
        case ErrorCode::ConfigError: return "configuration error";
        case ErrorCode::PluginError: return "plugin error";
        case ErrorCode::SyncError: return "synchronization error";
        default: return "unknown error";
        }
    }
};

inline const std::error_category& error_category() {
    static ErrorCategory instance;
    return instance;
}

inline std::error_code make_error_code(ErrorCode e) {
    return {static_cast<int>(e), error_category()};
}

struct Error {
    std::error_code code;
    std::string message;
    std::string context;
    Error(std::error_code c, std::string msg, std::string ctx = {})
        : code(c), message(std::move(msg)), context(std::move(ctx)) {}
    static Error from_code(ErrorCode c, std::string msg, std::string ctx = {}) {
        return Error(make_error_code(c), std::move(msg), std::move(ctx));
    }
};

template <typename T>
class Result {
    std::variant<T, Error> data_;
public:
    explicit Result(T value) : data_(std::move(value)) {}
    explicit Result(Error error) : data_(std::move(error)) {}
    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(data_); }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
    T& value() & {
        if (!ok()) throw std::system_error(std::get<Error>(data_).code, std::get<Error>(data_).message);
        return std::get<T>(data_);
    }
    T&& value() && {
        if (!ok()) throw std::system_error(std::get<Error>(data_).code, std::get<Error>(data_).message);
        return std::get<T>(std::move(data_));
    }
    const T& value() const& {
        if (!ok()) throw std::system_error(std::get<Error>(data_).code, std::get<Error>(data_).message);
        return std::get<T>(data_);
    }
    const Error& error() const& { return std::get<Error>(data_); }
    Error&& error() && { return std::get<Error>(std::move(data_)); }
    T value_or(const T& default_value) const& {
        if (ok()) return std::get<T>(data_);
        return default_value;
    }
    T value_or(T&& default_value) const& {
        if (ok()) return std::get<T>(data_);
        return std::move(default_value);
    }
    template <typename F>
    auto map(F&& f) -> Result<std::invoke_result_t<F, T&>> {
        using U = std::invoke_result_t<F, T&>;
        if (ok()) return Result<U>(std::forward<F>(f)(std::get<T>(data_)));
        return Result<U>(std::get<Error>(data_));
    }
};

template <>
class Result<void> {
    std::variant<std::monostate, Error> data_;
public:
    Result() : data_(std::monostate{}) {}
    explicit Result(Error error) : data_(std::move(error)) {}
    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<std::monostate>(data_); }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
    const Error& error() const& { return std::get<Error>(data_); }
    Error&& error() && { return std::get<Error>(std::move(data_)); }
};

} // namespace video2vec::core

namespace std {
template <>
struct is_error_code_enum<video2vec::core::ErrorCode> : true_type {};
} // namespace std
