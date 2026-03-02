#include "Entry/Entry.h"

#include "Config/ConfigManager.h"
#include "FloatingText/FloatingText.h"
#include "Utils/ItemTextureManager.h"
#include "command/command.h"
#include "compat/PLandCompat.h"
#include "db/Sqlite3Wrapper.h"
#include "interaction/Event.h"
#include "ll/api/mod/RegisterHelper.h"
#include "service/I18nService.h"

namespace CT {

Entry& Entry::getInstance() {
    static Entry instance;
    return instance;
}

bool Entry::load() {
    getSelf().getLogger().debug("Loading...");
    auto configPath = getSelf().getConfigDir();
    if (!std::filesystem::exists(configPath)) {
        std::filesystem::create_directories(configPath);
    }
    configPath /= "config.json";
    configPath.make_preferred();

    if (!ConfigManager::getInstance().load(configPath.string())) {
        getSelf().getLogger().error("Failed to load config file!");
        return false;
    }
    return true;
}

bool Entry::enable() {
    getSelf().getLogger().debug("Enabling...");

    const auto& config = CT::ConfigManager::getInstance().get();

    // 加载多语言
    CT::I18nService::getInstance().load(config.resourcePaths.languageDir, config.resourcePaths.language);

    // 按优先级顺序加载物品贴图文件（先加载的优先）
    CT::ItemTextureManager::getInstance().loadTextures(config.resourcePaths.itemTextureFiles);

    registerCommand();
    PLandCompat::getInstance().probe();

    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();

    const std::string& db_path = config.resourcePaths.databasePath; // 数据库文件路径
    if (db.open(db_path)) {
        getSelf().getLogger().info("Successfully opened database: " + db_path);

        // 启用数据库缓存，设置缓存时间为 60 秒
        db.enableCache(true);
        db.setCacheTimeout(60);
        getSelf().getLogger().info("Database cache enabled with 60s timeout.");

        FloatingTextManager::getInstance().loadAllChests(); // 在模组启用时加载所有悬浮字

        // 数据库打开成功后再注册事件监听器
        registerEventListener();
        registerPlayerConnectionListener(); // 注册玩家连接事件
    } else {
        getSelf().getLogger().error("Failed to open database: " + db_path);
        return false; // 数据库打开失败，模组启用失败
    }
    return true;
}

bool Entry::disable() {
    getSelf().getLogger().debug("Disabling...");
    // Code for disabling the mod goes here.
    FloatingTextManager::getInstance().stopDynamicTextUpdateLoop(); // 先停止协程
    FloatingTextManager::getInstance().removeAllFloatingTexts();    // 移除所有悬浮字
    Sqlite3Wrapper::getInstance().close();                          // 关闭数据库
    getSelf().getLogger().info("Database closed and all floating texts removed.");
    return true;
}

bool Entry::unload() {
    getSelf().getLogger().debug("Unloading...");
    // Unload 阶段主要用于资源清理
    // 大部分清理工作已在 disable() 中完成
    return true;
}

} // namespace CT


LL_REGISTER_MOD(CT::Entry, CT::Entry::getInstance());
