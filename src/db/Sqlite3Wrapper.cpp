#include "Sqlite3Wrapper.h"
#include "logger.h"
#include <iostream>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>

#include "Config/ConfigManager.h"

Sqlite3Wrapper::Sqlite3Wrapper() : db(nullptr), mThreadPool(nullptr) {}

Sqlite3Wrapper::~Sqlite3Wrapper() {
    close();
}

bool Sqlite3Wrapper::open(const std::string& db_path) {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }

    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    // 根据配置决定是否启用 WAL 模式以提高并发性能和安全性
    char* err_msg = nullptr;
    if (CT::ConfigManager::getInstance().get().enableWalMode) {
        if (sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
            CT::logger.warn("无法启用 WAL 模式: {}", err_msg ? err_msg : "未知错误");
            if (err_msg) sqlite3_free(err_msg);
        } else {
            CT::logger.info("WAL 模式已启用.");
        }
    } else {
        // 如果WAL模式未启用，确保使用默认的 delete 模式
        if (sqlite3_exec(db, "PRAGMA journal_mode=DELETE;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
            CT::logger.warn("无法设置为 DELETE 模式: {}", err_msg ? err_msg : "未知错误");
            if (err_msg) sqlite3_free(err_msg);
        } else {
            CT::logger.info("WAL 模式未启用, 使用 DELETE 模式.");
        }
    }

    // 启用外键约束
    if (sqlite3_exec(db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        CT::logger.warn("无法启用外键约束: {}", err_msg ? err_msg : "未知错误");
        if (err_msg) sqlite3_free(err_msg);
    }

    // 检查旧表 locked_chests 是否存在
    std::vector<std::vector<std::string>> tables = query_unsafe(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='locked_chests';"
    );
    
    if (!tables.empty()) {
        CT::logger.info("检测到旧数据表 `locked_chests`，正在迁移...");
        if (execute_unsafe("ALTER TABLE locked_chests RENAME TO chests;") &&
            execute_unsafe("ALTER TABLE chests ADD COLUMN type INTEGER DEFAULT 0;")) {
            CT::logger.info("数据表迁移成功！");
        } else {
            CT::logger.error("数据表迁移失败！");
            return false;
        }
    } else {
        const char* create_chests_table = "CREATE TABLE IF NOT EXISTS chests ("
                                          "player_uuid TEXT NOT NULL,"
                                          "dim_id INTEGER NOT NULL,"
                                          "pos_x INTEGER NOT NULL,"
                                          "pos_y INTEGER NOT NULL,"
                                          "pos_z INTEGER NOT NULL,"
                                          "type INTEGER DEFAULT 0,"
                                          "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z));";
        if (!execute_unsafe(create_chests_table)) {
            return false;
        }
    }

    const char* create_shared_chests_table = "CREATE TABLE IF NOT EXISTS shared_chests ("
                                             "player_uuid TEXT NOT NULL,"
                                             "owner_uuid TEXT NOT NULL,"
                                             "dim_id INTEGER NOT NULL,"
                                             "pos_x INTEGER NOT NULL,"
                                             "pos_y INTEGER NOT NULL,"
                                             "pos_z INTEGER NOT NULL,"
                                             "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES "
                                             "chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE);";
    if (!execute_unsafe(create_shared_chests_table)) {
        return false;
    }

    // 检查 shared_chests 表是否有 owner_uuid 字段，如果没有则添加
    if (!isColumnExists("shared_chests", "owner_uuid")) {
        CT::logger.info("检测到 `shared_chests` 表缺少 `owner_uuid` 字段，正在添加...");
        if (execute_unsafe("ALTER TABLE shared_chests ADD COLUMN owner_uuid TEXT NOT NULL DEFAULT '';")) {
            CT::logger.info("`owner_uuid` 字段添加成功！");
        } else {
            CT::logger.error("`owner_uuid` 字段添加失败！");
            return false;
        }
    }

    // 创建 item_definitions 表，用于存储唯一的 item_nbt 并映射到 item_id
    const char* create_item_definitions_table = "CREATE TABLE IF NOT EXISTS item_definitions ("
                                                "item_id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                                "item_nbt TEXT NOT NULL UNIQUE);";
    if (!execute_unsafe(create_item_definitions_table)) {
        return false;
    }

    // 检查旧的 shop_items 表是否存在且包含 item_nbt 列
    std::vector<std::vector<std::string>> shop_tables_info = query_unsafe("PRAGMA table_info(shop_items);");
    bool has_old_item_nbt_column = false;
    bool has_new_item_id_column = false;
    
    for (const auto& row : shop_tables_info) {
        if (row.size() > 1) {
            if (row[1] == "item_nbt") has_old_item_nbt_column = true;
            if (row[1] == "item_id") has_new_item_id_column = true;
        }
    }

    // 如果表存在且使用旧结构，需要迁移数据
    if (!shop_tables_info.empty() && has_old_item_nbt_column && !has_new_item_id_column) {
        CT::logger.info("检测到旧的 shop_items 表结构，开始迁移到新结构...");
        
        // 1. 将所有唯一的 item_nbt 插入到 item_definitions 表
        if (!execute_unsafe("INSERT OR IGNORE INTO item_definitions (item_nbt) SELECT DISTINCT item_nbt FROM shop_items;")) {
            CT::logger.error("迁移失败：无法插入 item_definitions");
            return false;
        }
        
        // 2. 创建新的 shop_items 表
        if (!execute_unsafe("ALTER TABLE shop_items RENAME TO shop_items_old;")) {
            CT::logger.error("迁移失败：无法重命名旧表");
            return false;
        }
        
        const char* create_new_shop_items = "CREATE TABLE shop_items ("
                                           "dim_id INTEGER NOT NULL,"
                                           "pos_x INTEGER NOT NULL,"
                                           "pos_y INTEGER NOT NULL,"
                                           "pos_z INTEGER NOT NULL,"
                                           "slot INTEGER NOT NULL,"
                                           "item_id INTEGER NOT NULL,"
                                           "price INTEGER NOT NULL,"
                                           "db_count INTEGER NOT NULL DEFAULT 0,"
                                           "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_id),"
                                           "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES "
                                           "chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE,"
                                           "FOREIGN KEY (item_id) REFERENCES item_definitions(item_id) ON DELETE CASCADE);";
        if (!execute_unsafe(create_new_shop_items)) {
            CT::logger.error("迁移失败：无法创建新表");
            return false;
        }
        
        // 3. 迁移数据
        const char* migrate_data = "INSERT INTO shop_items (dim_id, pos_x, pos_y, pos_z, slot, item_id, price, db_count) "
                                  "SELECT o.dim_id, o.pos_x, o.pos_y, o.pos_z, o.slot, d.item_id, o.price, o.db_count "
                                  "FROM shop_items_old o "
                                  "JOIN item_definitions d ON o.item_nbt = d.item_nbt;";
        if (!execute_unsafe(migrate_data)) {
            CT::logger.error("迁移失败：无法迁移数据");
            return false;
        }
        
        // 4. 删除旧表
        if (!execute_unsafe("DROP TABLE shop_items_old;")) {
            CT::logger.warn("警告：无法删除旧表 shop_items_old，请手动删除");
        }
        
        CT::logger.info("shop_items 表迁移成功！");
    } else if (shop_tables_info.empty()) {
        // 表不存在，创建新结构
        const char* create_shop_items_table = "CREATE TABLE IF NOT EXISTS shop_items ("
                                             "dim_id INTEGER NOT NULL,"
                                             "pos_x INTEGER NOT NULL,"
                                             "pos_y INTEGER NOT NULL,"
                                             "pos_z INTEGER NOT NULL,"
                                             "slot INTEGER NOT NULL,"
                                             "item_id INTEGER NOT NULL,"
                                             "price INTEGER NOT NULL,"
                                             "db_count INTEGER NOT NULL DEFAULT 0,"
                                             "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_id),"
                                             "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES "
                                             "chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE,"
                                             "FOREIGN KEY (item_id) REFERENCES item_definitions(item_id) ON DELETE CASCADE);";
        if (!execute_unsafe(create_shop_items_table)) {
            return false;
        }
    }

    std::vector<std::vector<std::string>> tables_info = query_unsafe("PRAGMA table_info(shop_items);");
    bool has_db_count_column = false;
    for (const auto& row : tables_info) {
        if (row.size() > 1 && row[1] == "db_count") {
            has_db_count_column = true;
            break;
        }
    }

    if (!has_db_count_column) {
        CT::logger.info("检测到 `shop_items` 表缺少 `db_count` 字段，正在添加...");
        if (execute_unsafe("ALTER TABLE shop_items ADD COLUMN db_count INTEGER NOT NULL DEFAULT 0;")) {
            CT::logger.info("`db_count` 字段添加成功！");
        } else {
            CT::logger.error("`db_count` 字段添加失败！");
            return false;
        }
    }

    // 检查 recycle_shop_items 表结构
    std::vector<std::vector<std::string>> recycle_shop_info = query_unsafe("PRAGMA table_info(recycle_shop_items);");
    bool has_item_nbt_in_recycle = false;
    bool has_item_id_in_recycle = false;

    for (const auto& row : recycle_shop_info) {
        if (row.size() > 1) {
            if (row[1] == "item_nbt") has_item_nbt_in_recycle = true;
            if (row[1] == "item_id") has_item_id_in_recycle = true;
        }
    }

    // 如果表存在且使用旧结构，需要迁移
    if (!recycle_shop_info.empty() && has_item_nbt_in_recycle && !has_item_id_in_recycle) {
        CT::logger.info("检测到旧的 recycle_shop_items 表结构，开始迁移...");

        execute_unsafe("INSERT OR IGNORE INTO item_definitions (item_nbt) SELECT DISTINCT item_nbt FROM recycle_shop_items;");
        execute_unsafe("ALTER TABLE recycle_shop_items RENAME TO recycle_shop_items_old;");

        const char* create_new_recycle_shop_items = "CREATE TABLE recycle_shop_items ("
                                                    "dim_id INTEGER NOT NULL,"
                                                    "pos_x INTEGER NOT NULL,"
                                                    "pos_y INTEGER NOT NULL,"
                                                    "pos_z INTEGER NOT NULL,"
                                                    "item_id INTEGER NOT NULL,"
                                                    "price INTEGER NOT NULL,"
                                                    "min_durability INTEGER NOT NULL DEFAULT 0,"
                                                    "required_enchants TEXT NOT NULL DEFAULT '',"
                                                    "max_recycle_count INTEGER NOT NULL DEFAULT 0,"
                                                    "current_recycled_count INTEGER NOT NULL DEFAULT 0,"
                                                    "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_id),"
                                                    "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE,"
                                                    "FOREIGN KEY (item_id) REFERENCES item_definitions(item_id) ON DELETE CASCADE);";
        if (!execute_unsafe(create_new_recycle_shop_items)) {
            CT::logger.error("迁移失败：无法创建新 recycle_shop_items 表");
            return false;
        }
        
        // 迁移数据
        const char* migrate_recycle_data = "INSERT INTO recycle_shop_items (dim_id, pos_x, pos_y, pos_z, item_id, price, min_durability, required_enchants, max_recycle_count, current_recycled_count) "
                                           "SELECT o.dim_id, o.pos_x, o.pos_y, o.pos_z, d.item_id, o.price, o.min_durability, '', 0, 0 "
                                           "FROM recycle_shop_items_old o "
                                           "JOIN item_definitions d ON o.item_nbt = d.item_nbt;";
        if (!execute_unsafe(migrate_recycle_data)) {
            CT::logger.error("迁移失败：无法迁移 recycle_shop_items 数据");
            return false;
        }

        execute_unsafe("DROP TABLE recycle_shop_items_old;");
        CT::logger.info("recycle_shop_items 表迁移成功！");
    } else if (recycle_shop_info.empty()) {
        const char* create_recycle_shop_items_table = "CREATE TABLE IF NOT EXISTS recycle_shop_items ("
                                                      "dim_id INTEGER NOT NULL,"
                                                      "pos_x INTEGER NOT NULL,"
                                                      "pos_y INTEGER NOT NULL,"
                                                      "pos_z INTEGER NOT NULL,"
                                                      "item_id INTEGER NOT NULL,"
                                                      "price INTEGER NOT NULL,"
                                                      "min_durability INTEGER NOT NULL DEFAULT 0,"
                                                      "required_enchants TEXT NOT NULL DEFAULT '',"
                                                      "max_recycle_count INTEGER NOT NULL DEFAULT 0,"
                                                      "current_recycled_count INTEGER NOT NULL DEFAULT 0,"
                                                      "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_id),"
                                                      "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES "
                                                      "chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE,"
                                                      "FOREIGN KEY (item_id) REFERENCES item_definitions(item_id) ON DELETE CASCADE);";
        if (!execute_unsafe(create_recycle_shop_items_table)) {
            return false;
        }
    }

    // 检查 recycle_records 表结构
    std::vector<std::vector<std::string>> recycle_records_info = query_unsafe("PRAGMA table_info(recycle_records);");
    bool has_item_nbt_in_recycle_records = false;
    bool has_item_id_in_recycle_records = false;

    for (const auto& row : recycle_records_info) {
        if (row.size() > 1) {
            if (row[1] == "item_nbt") has_item_nbt_in_recycle_records = true;
            if (row[1] == "item_id") has_item_id_in_recycle_records = true;
        }
    }

    if (!recycle_records_info.empty() && has_item_nbt_in_recycle_records && !has_item_id_in_recycle_records) {
        CT::logger.info("检测到旧的 recycle_records 表结构，开始迁移...");

        execute_unsafe("ALTER TABLE recycle_records RENAME TO recycle_records_old;");

        const char* create_new_recycle_records = "CREATE TABLE recycle_records ("
                                                 "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                                 "dim_id INTEGER NOT NULL,"
                                                 "pos_x INTEGER NOT NULL,"
                                                 "pos_y INTEGER NOT NULL,"
                                                 "pos_z INTEGER NOT NULL,"
                                                 "item_id INTEGER NOT NULL,"
                                                 "recycler_uuid TEXT NOT NULL,"
                                                 "recycle_count INTEGER NOT NULL,"
                                                 "total_price INTEGER NOT NULL,"
                                                 "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                                                 "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_id) REFERENCES "
                                                 "recycle_shop_items(dim_id, pos_x, pos_y, pos_z, item_id) ON DELETE CASCADE);";
        if (!execute_unsafe(create_new_recycle_records)) {
            CT::logger.error("迁移失败：无法创建新 recycle_records 表");
            return false;
        }
        
        const char* migrate_recycle_records_data = "INSERT INTO recycle_records (id, dim_id, pos_x, pos_y, pos_z, item_id, recycler_uuid, recycle_count, total_price, timestamp) "
                                                   "SELECT o.id, o.dim_id, o.pos_x, o.pos_y, o.pos_z, d.item_id, o.recycler_uuid, o.recycle_count, o.total_price, o.timestamp "
                                                   "FROM recycle_records_old o "
                                                   "JOIN item_definitions d ON o.item_nbt = d.item_nbt;";
        if (!execute_unsafe(migrate_recycle_records_data)) {
            CT::logger.error("迁移失败：无法迁移 recycle_records 数据");
            return false;
        }

        execute_unsafe("DROP TABLE recycle_records_old;");
        CT::logger.info("recycle_records 表迁移成功！");
    } else if (recycle_records_info.empty()) {
        const char* create_recycle_records_table = "CREATE TABLE IF NOT EXISTS recycle_records ("
                                                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                                   "dim_id INTEGER NOT NULL,"
                                                   "pos_x INTEGER NOT NULL,"
                                                   "pos_y INTEGER NOT NULL,"
                                                   "pos_z INTEGER NOT NULL,"
                                                   "item_id INTEGER NOT NULL,"
                                                   "recycler_uuid TEXT NOT NULL,"
                                                   "recycle_count INTEGER NOT NULL,"
                                                   "total_price INTEGER NOT NULL,"
                                                   "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                                                   "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_id) REFERENCES "
                                                   "recycle_shop_items(dim_id, pos_x, pos_y, pos_z, item_id) ON DELETE "
                                                   "CASCADE);";
        if (!execute_unsafe(create_recycle_records_table)) {
            return false;
        }
    }

    // 检查 purchase_records 表结构
    std::vector<std::vector<std::string>> purchase_records_info = query_unsafe("PRAGMA table_info(purchase_records);");
    bool has_item_nbt_in_purchase = false;
    bool has_item_id_in_purchase = false;
    
    for (const auto& row : purchase_records_info) {
        if (row.size() > 1) {
            if (row[1] == "item_nbt") has_item_nbt_in_purchase = true;
            if (row[1] == "item_id") has_item_id_in_purchase = true;
        }
    }

    // 如果表存在且使用旧结构，需要迁移
    if (!purchase_records_info.empty() && has_item_nbt_in_purchase && !has_item_id_in_purchase) {
        CT::logger.info("检测到旧的 purchase_records 表结构，开始迁移...");
        
        if (!execute_unsafe("ALTER TABLE purchase_records RENAME TO purchase_records_old;")) {
            CT::logger.error("迁移失败：无法重命名 purchase_records");
            return false;
        }
        
        const char* create_new_purchase_records = "CREATE TABLE purchase_records ("
                                                  "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                                  "dim_id INTEGER NOT NULL,"
                                                  "pos_x INTEGER NOT NULL,"
                                                  "pos_y INTEGER NOT NULL,"
                                                  "pos_z INTEGER NOT NULL,"
                                                  "item_id INTEGER NOT NULL,"
                                                  "buyer_uuid TEXT NOT NULL,"
                                                  "purchase_count INTEGER NOT NULL,"
                                                  "total_price INTEGER NOT NULL,"
                                                  "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                                                  "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_id) REFERENCES "
                                                  "shop_items(dim_id, pos_x, pos_y, pos_z, item_id) ON DELETE CASCADE);";
        if (!execute_unsafe(create_new_purchase_records)) {
            CT::logger.error("迁移失败：无法创建新 purchase_records 表");
            return false;
        }
        
        // 迁移数据
        const char* migrate_purchase_data = "INSERT INTO purchase_records (id, dim_id, pos_x, pos_y, pos_z, item_id, buyer_uuid, purchase_count, total_price, timestamp) "
                                           "SELECT o.id, o.dim_id, o.pos_x, o.pos_y, o.pos_z, d.item_id, o.buyer_uuid, o.purchase_count, o.total_price, o.timestamp "
                                           "FROM purchase_records_old o "
                                           "JOIN item_definitions d ON o.item_nbt = d.item_nbt;";
        if (!execute_unsafe(migrate_purchase_data)) {
            CT::logger.error("迁移失败：无法迁移 purchase_records 数据");
            return false;
        }
        
        if (!execute_unsafe("DROP TABLE purchase_records_old;")) {
            CT::logger.warn("警告：无法删除旧表 purchase_records_old");
        }
        
        CT::logger.info("purchase_records 表迁移成功！");
    } else if (purchase_records_info.empty()) {
        // 表不存在，创建新结构
        const char* create_purchase_records_table = "CREATE TABLE IF NOT EXISTS purchase_records ("
                                                    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                                    "dim_id INTEGER NOT NULL,"
                                                    "pos_x INTEGER NOT NULL,"
                                                    "pos_y INTEGER NOT NULL,"
                                                    "pos_z INTEGER NOT NULL,"
                                                    "item_id INTEGER NOT NULL,"
                                                    "buyer_uuid TEXT NOT NULL,"
                                                    "purchase_count INTEGER NOT NULL,"
                                                    "total_price INTEGER NOT NULL,"
                                                    "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                                                    "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_id) REFERENCES "
                                                    "shop_items(dim_id, pos_x, pos_y, pos_z, item_id) ON DELETE CASCADE);";
        if (!execute_unsafe(create_purchase_records_table)) {
            return false;
        }
    }

    // 创建空间索引以优化箱子位置查询
    const char* create_chests_spatial_index = "CREATE INDEX IF NOT EXISTS idx_chests_position "
                                              "ON chests(dim_id, pos_x, pos_y, pos_z);";
    if (!execute_unsafe(create_chests_spatial_index)) {
        CT::logger.warn("无法创建箱子位置索引，查询性能可能受影响");
    } else {
        CT::logger.info("箱子位置索引创建成功");
    }

    // 为 shared_chests 创建索引
    const char* create_shared_chests_index = "CREATE INDEX IF NOT EXISTS idx_shared_chests_position "
                                            "ON shared_chests(dim_id, pos_x, pos_y, pos_z);";
    if (!execute_unsafe(create_shared_chests_index)) {
        CT::logger.warn("无法创建共享箱子位置索引");
    }

    // 为 shop_items 创建索引
    const char* create_shop_items_index = "CREATE INDEX IF NOT EXISTS idx_shop_items_position "
                                         "ON shop_items(dim_id, pos_x, pos_y, pos_z);";
    if (!execute_unsafe(create_shop_items_index)) {
        CT::logger.warn("无法创建商店物品位置索引");
    }

    // 检查并添加新列到 recycle_shop_items
    if (!isColumnExists("recycle_shop_items", "required_enchants")) {
        execute_unsafe("ALTER TABLE recycle_shop_items ADD COLUMN required_enchants TEXT NOT NULL DEFAULT '';");
    }
    if (!isColumnExists("recycle_shop_items", "max_recycle_count")) {
        execute_unsafe("ALTER TABLE recycle_shop_items ADD COLUMN max_recycle_count INTEGER NOT NULL DEFAULT 0;");
    }
    if (!isColumnExists("recycle_shop_items", "current_recycled_count")) {
        execute_unsafe("ALTER TABLE recycle_shop_items ADD COLUMN current_recycled_count INTEGER NOT NULL DEFAULT 0;");
    }

    // 初始化线程池
    if (!mThreadPool) {
        mThreadPool = std::make_unique<ThreadPool>(mThreadPoolSize);
        CT::logger.info("数据库线程池已初始化，线程数: {}", mThreadPoolSize);
    }

    return true;
}

void Sqlite3Wrapper::close() {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
    clearCache();
}

bool Sqlite3Wrapper::execute_unsafe(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    char* err_msg = nullptr;
    if (sqlite3_exec(db, sql.c_str(), 0, 0, &err_msg) != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return false;
    }
    clearCache();  // 清除缓存，因为数据已更改
    return true;
}

static int callback(void* data, int argc, char** argv, char** azColName) {
    auto* results = static_cast<std::vector<std::vector<std::string>>*>(data);
    std::vector<std::string> row;
    for (int i = 0; i < argc; i++) {
        row.push_back(argv[i] ? argv[i] : "NULL");
    }
    results->push_back(row);
    return 0;
}

std::vector<std::vector<std::string>> Sqlite3Wrapper::query_unsafe(const std::string& sql) {
    std::vector<std::vector<std::string>> results;
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    char* err_msg = nullptr;
    if (sqlite3_exec(db, sql.c_str(), callback, &results, &err_msg) != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
    }
    return results;
}

bool Sqlite3Wrapper::beginTransaction() {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    if (mInTransaction) {
        return true;
    }
    
    if (!db) return false;

    char* err_msg = nullptr;
    if (sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "Failed to begin transaction: " << err_msg << std::endl;
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }
    
    mInTransaction = true;
    mStats.transactionCount++;
    return true;
}

bool Sqlite3Wrapper::commit() {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    if (!mInTransaction || !db) {
        return false;
    }

    char* err_msg = nullptr;
    if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "Failed to commit transaction: " << err_msg << std::endl;
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }
    
    mInTransaction = false;
    clearCache();  // 提交后清除缓存
    return true;
}

bool Sqlite3Wrapper::rollback() {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    if (!mInTransaction || !db) {
        return false;
    }

    char* err_msg = nullptr;
    if (sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "Failed to rollback transaction: " << err_msg << std::endl;
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }
    
    mInTransaction = false;
    clearCache();  // 回滚后清除缓存
    return true;
}

void Sqlite3Wrapper::clearCache() {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    mQueryCache.clear();
}

void Sqlite3Wrapper::setCacheTimeout(int seconds) {
    mCacheTimeoutSeconds = seconds;
}

void Sqlite3Wrapper::enableCache(bool enable) {
    mCacheEnabled = enable;
    if (!enable) {
        clearCache();
    }
}

bool Sqlite3Wrapper::isColumnExists(const std::string& tableName, const std::string& columnName) {
    if (!db) return false;

    std::string sql = "PRAGMA table_info(" + tableName + ");";
    std::vector<std::vector<std::string>> result = query_unsafe(sql);

    for (const auto& row : result) {
        if (row.size() > 1 && row[1] == columnName) {
            return true;
        }
    }
    return false;
}

std::string Sqlite3Wrapper::generateCacheKey(const std::string& sql, const std::vector<Value>& params) {
    std::stringstream ss;
    ss << sql;
    
    for (const auto& param : params) {
        ss << "|";
        std::visit([&ss](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int>) {
                ss << arg;
            } else if constexpr (std::is_same_v<T, long long>) {
                ss << arg;
            } else if constexpr (std::is_same_v<T, double>) {
                ss << std::fixed << std::setprecision(10) << arg;
            } else if constexpr (std::is_same_v<T, std::string>) {
                ss << arg;
            } else if constexpr (std::is_same_v<T, const char*>) {
                ss << arg;
            }
        }, param);
    }
    
    return ss.str();
}

bool Sqlite3Wrapper::getCachedResult(const std::string& key, std::vector<std::vector<std::string>>& result) {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    
    auto it = mQueryCache.find(key);
    if (it == mQueryCache.end()) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count();
    
    if (elapsed > mCacheTimeoutSeconds) {
        mQueryCache.erase(it);
        return false;
    }

    result = it->second.result;
    return true;
}

void Sqlite3Wrapper::setCachedResult(const std::string& key, const std::vector<std::vector<std::string>>& result) {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    
    mQueryCache[key] = {
        result,
        std::chrono::steady_clock::now()
    };
}

int Sqlite3Wrapper::getOrCreateItemId(const std::string& itemNbt) {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    
    if (!db) return -1;

    // 首先尝试查找是否已存在
    auto results = query("SELECT item_id FROM item_definitions WHERE item_nbt = ?", itemNbt);
    
    if (!results.empty()) {
        // 已存在，返回 item_id
        return std::stoi(results[0][0]);
    }

    // 不存在，插入新记录
    if (execute("INSERT INTO item_definitions (item_nbt) VALUES (?)", itemNbt)) {
        // 获取刚插入的 item_id
        long long lastId = sqlite3_last_insert_rowid(db);
        return static_cast<int>(lastId);
    }

    CT::logger.error("Failed to insert item_nbt into item_definitions");
    return -1;
}

std::string Sqlite3Wrapper::getItemNbtById(int itemId) {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    
    if (!db) return "";

    auto results = query("SELECT item_nbt FROM item_definitions WHERE item_id = ?", itemId);
    
    if (!results.empty()) {
        return results[0][0];
    }

    CT::logger.warn("Item ID {} not found in item_definitions", itemId);
    return "";
}

// === 异步操作实现 ===

void Sqlite3Wrapper::setThreadPoolSize(size_t size) {
    mThreadPoolSize = size;
}

size_t Sqlite3Wrapper::getPendingAsyncTasks() const {
    if (!mThreadPool) {
        return 0;
    }
    return mThreadPool->getPendingTaskCount();
}

void Sqlite3Wrapper::waitForAllAsyncTasks() {
    if (mThreadPool) {
        mThreadPool->waitForAllTasks();
    }
}

std::future<bool> Sqlite3Wrapper::executeBatchAsync(
    const std::vector<std::string>& sqlStatements,
    const std::vector<std::vector<Value>>& paramsList) {
    
    if (!mThreadPool) {
        throw std::runtime_error("Thread pool not initialized. Call open() first.");
    }
    
    return mThreadPool->enqueue([this, sqlStatements, paramsList]() {
        return this->executeBatch(sqlStatements, paramsList);
    });
}
