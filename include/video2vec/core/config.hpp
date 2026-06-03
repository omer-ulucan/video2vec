#pragma once

#include <any>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace video2vec::core {

class ConfigNode;
using ConfigValue = std::variant<bool, int64_t, double, std::string,
                                 std::vector<ConfigNode>, std::map<std::string, ConfigNode>>;

class ConfigNode {
public:
    ConfigNode() = default;
    explicit ConfigNode(ConfigValue value) : value_(std::move(value)) {}
    [[nodiscard]] bool has(const std::string& key) const;
    [[nodiscard]] const ConfigNode& operator[](const std::string& key) const;
    ConfigNode& operator[](const std::string& key);
    template <typename T>
    [[nodiscard]] std::optional<T> get() const {
        if (std::holds_alternative<T>(value_)) return std::get<T>(value_);
        return std::nullopt;
    }
    template <typename T>
    T value_or(T default_value) const {
        if (auto v = get<T>()) return *v;
        return default_value;
    }
    [[nodiscard]] bool is_map() const;
    [[nodiscard]] bool is_array() const;
    [[nodiscard]] bool is_scalar() const;
    [[nodiscard]] const std::map<std::string, ConfigNode>& as_map() const;
    [[nodiscard]] const std::vector<ConfigNode>& as_array() const;
    void set(const std::string& key, ConfigValue value);
    void push(ConfigValue value);
    void merge(const ConfigNode& other);
private:
    ConfigValue value_;
};

class Config {
public:
    Config() = default;
    static Config from_yaml(const std::string& path);
    static Config from_json(const std::string& path);
    static Config from_toml(const std::string& path);
    static Config from_string(const std::string& content, const std::string& format_hint = "yaml");
    [[nodiscard]] const ConfigNode& root() const { return root_; }
    ConfigNode& root() { return root_; }
    [[nodiscard]] bool has(const std::string& path) const;
    [[nodiscard]] const ConfigNode& get(const std::string& path) const;
    template <typename T>
    [[nodiscard]] T get_value(const std::string& path, T default_value) const {
        return get(path).value_or(std::move(default_value));
    }
    void set(const std::string& path, ConfigValue value);
    void merge(const Config& other);
    void validate(const std::vector<std::string>& required_paths) const;
private:
    ConfigNode root_;
};

} // namespace video2vec::core
