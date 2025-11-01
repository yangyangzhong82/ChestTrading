#include "Entry/Entry.h"

#include "FloatingText/FloatingText.h" // 引入 FloatingTextManager
#include "Utils/ItemTextureManager.h"  // 引入 ItemTextureManager
#include "db/Sqlite3Wrapper.h"
#include "interaction/event.h"
#include "ll/api/Config.h"
#include "ll/api/mod/RegisterHelper.h"

namespace CT {

Entry& Entry::getInstance() {
    static Entry instance;
    return instance;
}

bool Entry::load() {
    getSelf().getLogger().debug("Loading...");
    // Code for loading the mod goes here.
    return true;
}

bool Entry::enable() {
    getSelf().getLogger().debug("Enabling...");
    // Code for enabling the mod goes here.
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

    std::string db_path = "ChestTrading.db"; // 数据库文件路径
    if (db.open(db_path)) {
        getSelf().getLogger().info("Successfully opened database: " + db_path);
        // 创建 items 表（如果不存在）
        std::string create_items_table_sql =
            "CREATE TABLE IF NOT EXISTS items (id INTEGER PRIMARY KEY, name TEXT, quantity INTEGER);";
        if (db.execute(create_items_table_sql)) {
            getSelf().getLogger().info("Successfully created table: items");
        } else {
            getSelf().getLogger().error("Failed to create table: items");
        }

        // 创建 chests 表（如果不存在）
        std::string create_chests_table_sql = "CREATE TABLE IF NOT EXISTS chests ("
                                              "player_uuid TEXT NOT NULL,"
                                              "dim_id INTEGER NOT NULL,"
                                              "pos_x INTEGER NOT NULL,"
                                              "pos_y INTEGER NOT NULL,"
                                              "pos_z INTEGER NOT NULL,"
                                              "type INTEGER NOT NULL DEFAULT 0," // 添加 type 字段，默认为 0 (Locked)
                                              "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z)"
                                              ");";
        if (db.execute(create_chests_table_sql)) {
            getSelf().getLogger().info("Successfully created table: chests");
        } else {
            getSelf().getLogger().error("Failed to create table: chests");
        }

        // 创建 shared_chests 表（如果不存在）
        std::string create_shared_chests_table_sql = "CREATE TABLE IF NOT EXISTS shared_chests ("
                                                     "player_uuid TEXT NOT NULL,"
                                                     "dim_id INTEGER NOT NULL,"
                                                     "pos_x INTEGER NOT NULL,"
                                                     "pos_y INTEGER NOT NULL,"
                                                     "pos_z INTEGER NOT NULL,"
                                                     "PRIMARY KEY (player_uuid, dim_id, pos_x, pos_y, pos_z)"
                                                     ");";
        if (db.execute(create_shared_chests_table_sql)) {
            getSelf().getLogger().info("Successfully created table: shared_chests");
        } else {
            getSelf().getLogger().error("Failed to create table: shared_chests");
        }

        // 悬浮字将在第一个玩家加入时加载

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
