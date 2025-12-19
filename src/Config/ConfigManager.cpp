#include "ConfigManager.h"
#include "ConfigSerialization.h"
#include "Entry/Entry.h"
#include "config.h"
#include "logger.h"
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <vector>

namespace CT {

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

ConfigManager::ConfigManager() : mConfig(std::make_unique<Config>()) {}

ConfigManager::~ConfigManager() = default;

bool ConfigManager::load(const std::string& path) {
    mConfigPath = path;
    std::ifstream file(path);
    if (!file.is_open()) {
        // 文件不存在，创建默认配置
        logger.info("Config file not found, creating a new one at: {}", path);
        return save();
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    nlohmann::json user_json;
    try {
        // 解析用户配置，允许注释
        user_json = nlohmann::json::parse(content, nullptr, true, true);
        // 填充内存中的配置对象。缺失的键将从结构体定义中获取默认值。
        *mConfig = user_json.get<Config>();
    } catch (const nlohmann::json::exception& e) {
        logger.error("Failed to parse config file: {}. Please check for syntax errors.", e.what());
        return false;
    }

    // 检查缺失的键以警告用户。
    nlohmann::json default_json = Config{}; // 创建一个默认配置对象并转换为json

    // 递归函数查找缺失的键
    std::function<void(const nlohmann::json&, const nlohmann::json&, std::string, std::vector<std::string>&)>
        find_missing_keys =
            [&](const nlohmann::json& d, const nlohmann::json& u, std::string p, std::vector<std::string>& m) {
                for (const auto& item : d.items()) {
                    if (!u.contains(item.key())) {
                        m.push_back(p + item.key());
                    } else if (item.value().is_object() && u.at(item.key()).is_object()) {
                        find_missing_keys(item.value(), u.at(item.key()), p + item.key() + ".", m);
                    }
                }
            };

    std::vector<std::string> missing_keys;
    find_missing_keys(default_json, user_json, "", missing_keys);

    if (!missing_keys.empty()) {
        auto& logger = Entry::getInstance().getSelf().getLogger();
        logger.warn("Your config file is missing some keys. The plugin will use default values for them.");
        logger.warn("This won't affect functionality, but you may want to add them to your config.json:");
        for (const auto& key : missing_keys) {
            logger.warn("- {}", key);
        }
        logger.warn("You can also delete your config.json to generate a new one with all keys on next startup.");
    }
    auto& logger = Entry::getInstance().getSelf().getLogger();
    logger.setLevel(mConfig->logLevel);

    return true; // 不再保存，以保留用户文件的格式和注释
}

bool ConfigManager::save() {
    try {
        nlohmann::json json = *mConfig;
        std::ofstream  file(mConfigPath);
        file << json.dump(4);
    } catch (const nlohmann::json::exception& e) {
        Entry::getInstance().getSelf().getLogger().error("Failed to save config file: {}", e.what());
        return false;
    }
    return true;
}

bool ConfigManager::reload() { return load(mConfigPath); }

Config& ConfigManager::get() { return *mConfig; }

const Config& ConfigManager::get() const { return *mConfig; }

} // namespace CT
