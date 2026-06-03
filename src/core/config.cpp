#include "video2vec/core/config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

namespace video2vec::core {

namespace {
    ConfigNode yaml_to_node(const YAML::Node& node);

    ConfigValue yaml_to_value(const YAML::Node& node) {
        switch (node.Type()) {
        case YAML::NodeType::Scalar: {
            try { return node.as<int64_t>(); } catch (...) {}
            try { return node.as<double>(); } catch (...) {}
            try { return node.as<bool>(); } catch (...) {}
            return node.as<std::string>();
        }
        case YAML::NodeType::Sequence: {
            std::vector<ConfigNode> values;
            for (const auto& item : node) values.push_back(yaml_to_node(item));
            return values;
        }
        case YAML::NodeType::Map: {
            std::map<std::string, ConfigNode> values;
            for (const auto& pair : node) values[pair.first.as<std::string>()] = yaml_to_node(pair.second);
            return values;
        }
        default: return std::string{};
        }
    }

    ConfigNode yaml_to_node(const YAML::Node& node) {
        return ConfigNode(yaml_to_value(node));
    }

    ConfigNode json_to_node(const nlohmann::json& j) {
        if (j.is_null()) return ConfigNode();
        if (j.is_boolean()) return ConfigNode(j.get<bool>());
        if (j.is_number_integer()) return ConfigNode(j.get<int64_t>());
        if (j.is_number_float()) return ConfigNode(j.get<double>());
        if (j.is_string()) return ConfigNode(j.get<std::string>());
        if (j.is_array()) {
            std::vector<ConfigNode> values;
            for (const auto& item : j) values.push_back(json_to_node(item));
            return ConfigNode(values);
        }
        if (j.is_object()) {
            std::map<std::string, ConfigNode> values;
            for (auto& [key, val] : j.items()) values[key] = json_to_node(val);
            return ConfigNode(values);
        }
        return ConfigNode();
    }
}

bool ConfigNode::has(const std::string& key) const {
    if (!is_map()) return false;
    auto& m = std::get<std::map<std::string, ConfigNode>>(value_);
    return m.find(key) != m.end();
}

const ConfigNode& ConfigNode::operator[](const std::string& key) const {
    static const ConfigNode empty{};
    if (!is_map()) return empty;
    auto& m = std::get<std::map<std::string, ConfigNode>>(value_);
    auto it = m.find(key);
    if (it != m.end()) return it->second;
    return empty;
}

ConfigNode& ConfigNode::operator[](const std::string& key) {
    if (!is_map()) value_ = std::map<std::string, ConfigNode>{};
    auto& m = std::get<std::map<std::string, ConfigNode>>(value_);
    return m[key];
}

bool ConfigNode::is_map() const { return std::holds_alternative<std::map<std::string, ConfigNode>>(value_); }
bool ConfigNode::is_array() const { return std::holds_alternative<std::vector<ConfigNode>>(value_); }
bool ConfigNode::is_scalar() const { return !is_map() && !is_array(); }

const std::map<std::string, ConfigNode>& ConfigNode::as_map() const {
    static const std::map<std::string, ConfigNode> empty;
    if (!is_map()) return empty;
    return std::get<std::map<std::string, ConfigNode>>(value_);
}

const std::vector<ConfigNode>& ConfigNode::as_array() const {
    static const std::vector<ConfigNode> empty;
    if (!is_array()) return empty;
    return std::get<std::vector<ConfigNode>>(value_);
}

void ConfigNode::set(const std::string& key, ConfigValue value) { (*this)[key] = ConfigNode(std::move(value)); }
void ConfigNode::push(ConfigValue value) {
    if (!is_array()) value_ = std::vector<ConfigNode>{};
    std::get<std::vector<ConfigNode>>(value_).push_back(ConfigNode(std::move(value)));
}

void ConfigNode::merge(const ConfigNode& other) {
    if (!other.is_map() || !is_map()) return;
    auto& this_map = std::get<std::map<std::string, ConfigNode>>(value_);
    for (const auto& [key, val] : other.as_map()) {
        if (val.is_map() && this_map[key].is_map()) this_map[key].merge(val);
        else this_map[key] = val;
    }
}

Config Config::from_yaml(const std::string& path) {
    Config cfg;
    cfg.root_ = yaml_to_node(YAML::LoadFile(path));
    return cfg;
}

Config Config::from_json(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open JSON config: " + path);
    nlohmann::json j;
    file >> j;
    Config cfg;
    cfg.root_ = json_to_node(j);
    return cfg;
}

Config Config::from_toml(const std::string& path) {
    (void)path;
    throw std::runtime_error("TOML support not compiled in");
}

Config Config::from_string(const std::string& content, const std::string& format_hint) {
    if (format_hint == "json") {
        Config cfg;
        cfg.root_ = json_to_node(nlohmann::json::parse(content));
        return cfg;
    }
    Config cfg;
    cfg.root_ = yaml_to_node(YAML::Load(content));
    return cfg;
}

bool Config::has(const std::string& path) const {
    size_t pos = 0;
    const ConfigNode* node = &root_;
    while (pos < path.size()) {
        size_t dot = path.find('.', pos);
        std::string key = path.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
        if (!node->has(key)) return false;
        node = &(*node)[key];
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    return true;
}

const ConfigNode& Config::get(const std::string& path) const {
    size_t pos = 0;
    const ConfigNode* node = &root_;
    while (pos < path.size()) {
        size_t dot = path.find('.', pos);
        std::string key = path.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
        node = &(*node)[key];
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    return *node;
}

void Config::set(const std::string& path, ConfigValue value) {
    size_t pos = 0;
    ConfigNode* node = &root_;
    while (pos < path.size()) {
        size_t dot = path.find('.', pos);
        std::string key = path.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
        if (dot == std::string::npos) {
            node->set(key, std::move(value));
            return;
        }
        if (!node->has(key)) node->set(key, std::map<std::string, ConfigNode>{});
        node = &(*node)[key];
        pos = dot + 1;
    }
}

void Config::merge(const Config& other) { root_.merge(other.root_); }

void Config::validate(const std::vector<std::string>& required_paths) const {
    for (const auto& path : required_paths) {
        if (!has(path)) throw std::runtime_error("missing required config path: " + path);
    }
}

} // namespace video2vec::core
