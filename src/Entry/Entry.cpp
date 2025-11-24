#include "Entry/Entry.h"

#include "Config/ConfigManager.h"
#include "FloatingText/FloatingText.h" 
#include "Utils/ItemTextureManager.h"  
#include "db/Sqlite3Wrapper.h"
#include "interaction/event.h"
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
    getSelf().getLogger().setLevel(ll::io::LogLevel::Trace);
    // 优先加载用户指定的 texture_path.json
    std::string customTexturePath = "texture_path.json";
    if (CT::ItemTextureManager::getInstance().loadTextures(customTexturePath)) {
        getSelf().getLogger().info("成功加载自定义物品贴图文件: {}", customTexturePath);
    } else {
        getSelf().getLogger().warn("无法加载自定义物品贴图文件: {}，将只使用默认文件。", customTexturePath);
    }

    // 加载默认物品贴图文件
    std::vector<std::string> defaultTextureFiles = {"terrain_texture.json", "item_texture.json"};
    CT::ItemTextureManager::getInstance().loadTextures(defaultTextureFiles);
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    registerEventListener();
    registerPlayerConnectionListener(); // 注册玩家连接事件

    std::string db_path = "plugins/ChestTrading/ChestTrading.db"; // 数据库文件路径
    if (db.open(db_path)) {
        getSelf().getLogger().info("Successfully opened database: " + db_path);

        // 启用数据库缓存，设置缓存时间为 60 秒
        db.enableCache(true);
        db.setCacheTimeout(60);
        getSelf().getLogger().info("Database cache enabled with 60s timeout.");

        FloatingTextManager::getInstance().loadAllLockedChests(); // 在模组启用时加载所有悬浮字

    } else {
        getSelf().getLogger().error("Failed to open database: " + db_path);
        return false; // 数据库打开失败，模组启用失败
    }
    return true;
}

bool Entry::disable() {
    getSelf().getLogger().debug("Disabling...");
    // Code for disabling the mod goes here.
    Sqlite3Wrapper::getInstance().close();                       // 关闭数据库
    FloatingTextManager::getInstance().removeAllFloatingTexts(); // 移除所有悬浮字
    getSelf().getLogger().info("Database closed and all floating texts removed.");
    return true;
}

} // namespace CT


LL_REGISTER_MOD(CT::Entry, CT::Entry::getInstance());
