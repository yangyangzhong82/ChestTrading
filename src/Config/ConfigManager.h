#pragma once

#include "config.h"
#include <memory>
#include <string>


namespace CT {

struct ReloadResult {
    bool success                = false;
    bool commandSettingsIgnored = false;
};

class ConfigManager {
public:
    // 获取单例实例
    static ConfigManager& getInstance();

    // 加载配置，如果文件不存在则创建默认配置
    bool load(const std::string& path);

    // 保存配置
    bool save();

    // 重新加载配置
    ReloadResult reload();

    // 获取配置对象的引用
    Config& get();

    // 获取配置对象的常量引用
    const Config& get() const;

private:
    ConfigManager();
    ~ConfigManager();

    ConfigManager(const ConfigManager&)            = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager(ConfigManager&&)                 = delete;
    ConfigManager& operator=(ConfigManager&&)      = delete;

    std::unique_ptr<Config> mConfig;
    std::string             mConfigPath;
};

} // namespace CT
