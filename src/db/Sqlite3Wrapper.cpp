#include "Sqlite3Wrapper.h"
#include "logger.h"
#include <iostream>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>

#include "Config/ConfigManager.h"
Sqlite3Wrapper::Sqlite3Wrapper() : db(nullptr) {}

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
                                             "dim_id INTEGER NOT NULL,"
                                             "pos_x INTEGER NOT NULL,"
                                             "pos_y INTEGER NOT NULL,"
                                             "pos_z INTEGER NOT NULL,"
                                             "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES "
                                             "chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE);";
    if (!execute_unsafe(create_shared_chests_table)) {
        return false;
    }

    const char* create_shop_items_table = "CREATE TABLE IF NOT EXISTS shop_items ("
                                          "dim_id INTEGER NOT NULL,"
                                          "pos_x INTEGER NOT NULL,"
                                          "pos_y INTEGER NOT NULL,"
                                          "pos_z INTEGER NOT NULL,"
                                          "slot INTEGER NOT NULL,"
                                          "item_nbt TEXT NOT NULL,"
                                          "price INTEGER NOT NULL,"
                                          "db_count INTEGER NOT NULL DEFAULT 0,"
                                          "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_nbt),"
                                          "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES "
                                          "chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE);";
    if (!execute_unsafe(create_shop_items_table)) {
        return false;
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

    const char* create_recycle_shop_items_table = "CREATE TABLE IF NOT EXISTS recycle_shop_items ("
                                                  "dim_id INTEGER NOT NULL,"
                                                  "pos_x INTEGER NOT NULL,"
                                                  "pos_y INTEGER NOT NULL,"
                                                  "pos_z INTEGER NOT NULL,"
                                                  "item_nbt TEXT NOT NULL,"
                                                  "price INTEGER NOT NULL,"
                                                  "min_durability INTEGER NOT NULL DEFAULT 0,"
                                                  "required_enchant_id INTEGER NOT NULL DEFAULT -1,"
                                                  "required_enchant_level INTEGER NOT NULL DEFAULT 0,"
                                                  "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_nbt),"
                                                  "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES "
                                                  "chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE);";
    if (!execute_unsafe(create_recycle_shop_items_table)) {
        return false;
    }

    const char* create_recycle_records_table = "CREATE TABLE IF NOT EXISTS recycle_records ("
                                               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                               "dim_id INTEGER NOT NULL,"
                                               "pos_x INTEGER NOT NULL,"
                                               "pos_y INTEGER NOT NULL,"
                                               "pos_z INTEGER NOT NULL,"
                                               "item_nbt TEXT NOT NULL,"
                                               "recycler_uuid TEXT NOT NULL,"
                                               "recycle_count INTEGER NOT NULL,"
                                               "total_price INTEGER NOT NULL,"
                                               "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                                               "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_nbt) REFERENCES "
                                               "recycle_shop_items(dim_id, pos_x, pos_y, pos_z, item_nbt) ON DELETE "
                                               "CASCADE);";
    if (!execute_unsafe(create_recycle_records_table)) {
        return false;
    }

    const char* create_purchase_records_table = "CREATE TABLE IF NOT EXISTS purchase_records ("
                                                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                                "dim_id INTEGER NOT NULL,"
                                                "pos_x INTEGER NOT NULL,"
                                                "pos_y INTEGER NOT NULL,"
                                                "pos_z INTEGER NOT NULL,"
                                                "item_nbt TEXT NOT NULL,"
                                                "buyer_uuid TEXT NOT NULL,"
                                                "purchase_count INTEGER NOT NULL,"
                                                "total_price INTEGER NOT NULL,"
                                                "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
                                                "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_nbt) REFERENCES "
                                                "shop_items(dim_id, pos_x, pos_y, pos_z, item_nbt) ON DELETE "
                                                "CASCADE);";
    if (!execute_unsafe(create_purchase_records_table)) {
        return false;
    }

    std::vector<std::vector<std::string>> recycle_tables_info = query_unsafe("PRAGMA table_info(recycle_shop_items);");
    bool has_min_durability_column = false;
    bool has_req_enchant_id_column = false;
    bool has_req_enchant_lvl_column = false;

    for (const auto& row : recycle_tables_info) {
        if (row.size() > 1) {
            if (row[1] == "min_durability") has_min_durability_column = true;
            if (row[1] == "required_enchant_id") has_req_enchant_id_column = true;
            if (row[1] == "required_enchant_level") has_req_enchant_lvl_column = true;
        }
    }

    if (!has_min_durability_column) {
        CT::logger.info("检测到 `recycle_shop_items` 表缺少 `min_durability` 字段，正在添加...");
        if (!execute_unsafe("ALTER TABLE recycle_shop_items ADD COLUMN min_durability INTEGER NOT NULL DEFAULT 0;")) {
            CT::logger.error("`min_durability` 字段添加失败！");
            return false;
        }
    }

    if (!has_req_enchant_id_column) {
        CT::logger.info("检测到 `recycle_shop_items` 表缺少 `required_enchant_id` 字段，正在添加...");
        if (!execute_unsafe("ALTER TABLE recycle_shop_items ADD COLUMN required_enchant_id INTEGER NOT NULL DEFAULT -1;")) {
            CT::logger.error("`required_enchant_id` 字段添加失败！");
            return false;
        }
    }

    if (!has_req_enchant_lvl_column) {
        CT::logger.info("检测到 `recycle_shop_items` 表缺少 `required_enchant_level` 字段，正在添加...");
        if (!execute_unsafe("ALTER TABLE recycle_shop_items ADD COLUMN required_enchant_level INTEGER NOT NULL DEFAULT 0;")) {
            CT::logger.error("`required_enchant_level` 字段添加失败！");
            return false;
        }
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
