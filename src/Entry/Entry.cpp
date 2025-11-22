#include "Entry/Entry.h"

#include "Config/ConfigManager.h"
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

    std::string db_path = "plugins/ChestTrading/ChestTrading.db"; // 数据库文件路径
    if (db.open(db_path)) {
        getSelf().getLogger().info("Successfully opened database: " + db_path);

        // 启用数据库缓存，设置缓存时间为 60 秒
        db.enableCache(true);
        db.setCacheTimeout(60);
        getSelf().getLogger().info("Database cache enabled with 60s timeout.");

        // 开启事务处理初始化操作
        if (db.beginTransaction()) {
            // 创建 item_definitions 表，用于存储唯一的 item_nbt 并映射到 item_id
            std::string create_item_definitions_table = "CREATE TABLE IF NOT EXISTS item_definitions ("
                                                        "item_id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                                        "item_nbt TEXT NOT NULL UNIQUE);";
            if (db.execute(create_item_definitions_table)) {
                getSelf().getLogger().info("Successfully created table: item_definitions");
            } else {
                getSelf().getLogger().error("Failed to create table: item_definitions");
            }

            // 创建 items 表（如果不存在）
            std::string create_items_table_sql =
                "CREATE TABLE IF NOT EXISTS items (id INTEGER PRIMARY KEY, name TEXT, quantity INTEGER);";
            if (db.execute(create_items_table_sql)) {
                getSelf().getLogger().info("Successfully created table: items");
            } else {
                getSelf().getLogger().error("Failed to create table: items");
            }

            // 创建 chests 表（如果不存在）
            std::string create_chests_table_sql =
                "CREATE TABLE IF NOT EXISTS chests ("
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

            // 提交事务
            db.commit();
        } else {
            getSelf().getLogger().error("Failed to begin transaction for database initialization.");
        }

        FloatingTextManager::getInstance().loadAllLockedChests(); // 在模组启用时加载所有悬浮字

        // --- Shop & Recycle Shop Tables ---
        // 使用新的事务块处理商店表
        if (db.beginTransaction()) {
            // 创建 shop_items 表
            std::string create_shop_items_sql =
                "CREATE TABLE IF NOT EXISTS shop_items ("
                "dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL,"
                "slot INTEGER, item_id INTEGER NOT NULL, price INTEGER NOT NULL, db_count INTEGER NOT NULL DEFAULT 0,"
                "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_id),"
                "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE,"
                "FOREIGN KEY (item_id) REFERENCES item_definitions(item_id) ON DELETE CASCADE"
                ");";
            if (!db.execute(create_shop_items_sql)) {
                getSelf().getLogger().error("Failed to create table: shop_items");
            } else {
                getSelf().getLogger().info("Successfully created table: shop_items");
            }

            // 创建 purchase_records 表
            std::string create_purchase_records_sql =
                "CREATE TABLE IF NOT EXISTS purchase_records ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL,"
                "item_id INTEGER NOT NULL, buyer_uuid TEXT NOT NULL, purchase_count INTEGER NOT NULL, total_price "
                "NOT NULL,"
                "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_id) REFERENCES shop_items(dim_id, pos_x, pos_y, pos_z, item_id) ON DELETE CASCADE"
                ");";
            if (!db.execute(create_purchase_records_sql)) {
                getSelf().getLogger().error("Failed to create table: purchase_records");
            } else {
                getSelf().getLogger().info("Successfully created table: purchase_records");
            }

            // 创建 recycle_shop_items 表 (使用新的 required_enchants 字段)
            std::string create_recycle_shop_items_sql =
                "CREATE TABLE IF NOT EXISTS recycle_shop_items ("
                "dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL,"
                "item_id INTEGER NOT NULL, price INTEGER NOT NULL, min_durability INTEGER DEFAULT 0,"
                "required_enchants TEXT NOT NULL DEFAULT '', " // 新增：存储附魔要求的JSON字符串
                "max_recycle_count INTEGER NOT NULL DEFAULT 0, current_recycled_count INTEGER NOT NULL DEFAULT 0,"
                "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_id),"
                "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE,"
                "FOREIGN KEY (item_id) REFERENCES item_definitions(item_id) ON DELETE CASCADE"
                ");";
            if (!db.execute(create_recycle_shop_items_sql)) {
                getSelf().getLogger().error("Failed to create table: recycle_shop_items");
            } else {
                getSelf().getLogger().info("Successfully created table: recycle_shop_items");
                // 检查旧的附魔列是否存在，如果存在则删除 (用于平滑升级)
                // 注意：DDL操作在某些数据库中可能会隐式提交事务，但在SQLite中通常可以在事务中执行
                if (db.isColumnExists("recycle_shop_items", "required_enchant_id")) {
                    db.execute("ALTER TABLE recycle_shop_items DROP COLUMN required_enchant_id;");
                    getSelf().getLogger().info("Dropped legacy column 'required_enchant_id' from 'recycle_shop_items'."
                    );
                }
                if (db.isColumnExists("recycle_shop_items", "required_enchant_level")) {
                    db.execute("ALTER TABLE recycle_shop_items DROP COLUMN required_enchant_level;");
                    getSelf().getLogger().info(
                        "Dropped legacy column 'required_enchant_level' from 'recycle_shop_items'."
                    );
                }
            }

            // 创建 recycle_records 表
            std::string create_recycle_records_sql =
                "CREATE TABLE IF NOT EXISTS recycle_records ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL,"
                "item_id INTEGER NOT NULL, recycler_uuid TEXT NOT NULL, recycle_count INTEGER NOT NULL, total_price "
                "INTEGER "
                "NOT NULL,"
                "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_id) REFERENCES recycle_shop_items(dim_id, pos_x, pos_y, pos_z, item_id) ON DELETE CASCADE"
                ");";
            if (!db.execute(create_recycle_records_sql)) {
                getSelf().getLogger().error("Failed to create table: recycle_records");
            } else {
                getSelf().getLogger().info("Successfully created table: recycle_records");
            }

            // 提交商店表相关的事务
            db.commit();
        } else {
            getSelf().getLogger().error("Failed to begin transaction for shop tables initialization.");
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
    Sqlite3Wrapper::getInstance().close();                       // 关闭数据库
    FloatingTextManager::getInstance().removeAllFloatingTexts(); // 移除所有悬浮字
    getSelf().getLogger().info("Database closed and all floating texts removed.");
    return true;
}

} // namespace CT


LL_REGISTER_MOD(CT::Entry, CT::Entry::getInstance());
