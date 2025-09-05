#include "Entry/Entry.h"

#include "ll/api/Config.h"
#include "ll/api/mod/RegisterHelper.h"
#include "db/Sqlite3Wrapper.h"

#include "interaction/event.h"
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
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    registerEventListener();
    std::string db_path = "ChestTrading.db"; // 数据库文件路径
    if (db.open(db_path)) {
        getSelf().getLogger().info("Successfully opened database: " + db_path);
        // 创建 items 表（如果不存在）
        std::string create_items_table_sql = "CREATE TABLE IF NOT EXISTS items (id INTEGER PRIMARY KEY, name TEXT, quantity INTEGER);";
        if (db.execute(create_items_table_sql)) {
            getSelf().getLogger().info("Successfully created table: items");
        } else {
            getSelf().getLogger().error("Failed to create table: items");
        }
        
        // 创建 locked_chests 表（如果不存在）
        std::string create_locked_chests_table_sql = 
            "CREATE TABLE IF NOT EXISTS locked_chests ("
            "player_uuid TEXT NOT NULL,"
            "dim_id INTEGER NOT NULL,"
            "pos_x INTEGER NOT NULL,"
            "pos_y INTEGER NOT NULL,"
            "pos_z INTEGER NOT NULL,"
            "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z)"
            ");";
        if (db.execute(create_locked_chests_table_sql)) {
            getSelf().getLogger().info("Successfully created table: locked_chests");
        } else {
            getSelf().getLogger().error("Failed to create table: locked_chests");
        }
    } else {
        getSelf().getLogger().error("Failed to open database: " + db_path);
        return false; // 数据库打开失败，模组启用失败
    }
    return true;
}

bool Entry::disable() {
    getSelf().getLogger().debug("Disabling...");
    // Code for disabling the mod goes here.
    Sqlite3Wrapper::getInstance().close(); // 关闭数据库
    getSelf().getLogger().info("Database closed.");
    return true;
}

} // namespace CT

LL_REGISTER_MOD(CT::Entry, CT::Entry::getInstance());
