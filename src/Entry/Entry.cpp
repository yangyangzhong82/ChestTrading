#include "Entry/Entry.h"

#include "Config/ConfigManager.h"
#include "FloatingText/FloatingText.h"
#include "Utils/ItemTextureManager.h"
#include "command/command.h"
#include "db/Sqlite3Wrapper.h"
#include "interaction/Event.h"
#include "ll/api/mod/RegisterHelper.h"

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

    // 优先加载用户指定的自定义物品贴图文件
    const std::string& customTexturePath = config.resourcePaths.customItemTextureFile;
    if (CT::ItemTextureManager::getInstance().loadTextures(customTexturePath)) {
        getSelf().getLogger().info("成功加载自定义物品贴图文件: {}", customTexturePath);
    } else {
        getSelf().getLogger().warn("无法加载自定义物品贴图文件: {}，将只使用默认文件。", customTexturePath);
    }

    registerCommand();

    // 加载默认物品贴图文件
    CT::ItemTextureManager::getInstance().loadTextures(config.resourcePaths.defaultItemTextureFiles);

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

} // namespace CT


LL_REGISTER_MOD(CT::Entry, CT::Entry::getInstance());
