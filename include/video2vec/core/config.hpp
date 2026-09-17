#pragma once

#include <any>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
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
    // Exact variant alternative lookup (bool, int64_t, double, std::string, ...).
    template <typename T>
    [[nodiscard]] std::optional<T> get() const {
        if (std::holds_alternative<T>(value_)) return std::get<T>(value_);
        return std::nullopt;
    }
    // Lookup with arithmetic conversion: any integral T reads the int64_t
    // alternative, any floating-point T reads double (or int64_t), so
    // get_as<int>() and get_as<float>() work as expected.
    template <typename T>
    [[nodiscard]] std::optional<T> get_as() const {
        if constexpr (std::is_same_v<T, bool>) {
            return get<bool>();
        } else if constexpr (std::is_integral_v<T>) {
            if (auto v = get<int64_t>()) return static_cast<T>(*v);
            return std::nullopt;
        } else if constexpr (std::is_floating_point_v<T>) {
            if (auto v = get<double>()) return static_cast<T>(*v);
            if (auto v = get<int64_t>()) return static_cast<T>(*v);
            return std::nullopt;
        } else {
            return get<T>();
        }
    }
    template <typename T>
    T value_or(T default_value) const {
        if (auto v = get_as<T>()) return *v;
        return default_value;
    }
    [[nodiscard]] bool is_map() const;
    [[nodiscard]] bool is_array() const;
    [[nodiscard]] bool is_scalar() const;
    [[nodiscard]] const std::map<std::string, ConfigNode>& as_map() const;
    [[nodiscard]] const std::vector<ConfigNode>& as_array() const;
    void set(const std::string& key, ConfigValue value);
    void push(ConfigValue value);
    // Deep-merges `other` (a map) into this node. A node that is not yet a
    // map (e.g. a default-constructed root) becomes one.
    void merge(const ConfigNode& other);
private:
    ConfigValue value_;
};

class Config {
public:
    Config() = default;
    // All loaders throw std::runtime_error (never a third-party exception
    // type) when the file cannot be read or parsed.
    static Config from_yaml(const std::string& path);
    static Config from_json(const std::string& path);
    static Config from_toml(const std::string& path);
    static Config from_string(const std::string& content, const std::string& format_hint = "yaml");
    [[nodiscard]] const ConfigNode& root() const { return root_; }
    ConfigNode& root() { return root_; }
    [[nodiscard]] bool has(const std::string& path) const;
    // Throws std::out_of_range if the dotted path does not exist.
    [[nodiscard]] const ConfigNode& get(const std::string& path) const;
    // Returns default_value when the path is missing or holds an incompatible type.
    template <typename T>
    [[nodiscard]] T get_value(const std::string& path, T default_value) const {
        if (!has(path)) return default_value;
        return get(path).template value_or<T>(std::move(default_value));
    }
    [[nodiscard]] std::string get_value(const std::string& path, const char* default_value) const {
        return get_value<std::string>(path, std::string(default_value));
    }
    void set(const std::string& path, ConfigValue value);
    void merge(const Config& other);
    void validate(const std::vector<std::string>& required_paths) const;
private:
    ConfigNode root_;
};

} // namespace video2vec::core
