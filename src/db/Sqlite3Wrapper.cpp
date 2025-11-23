#include "Sqlite3Wrapper.h"
#include "logger.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>


#include "Config/ConfigManager.h"

Sqlite3Wrapper::Sqlite3Wrapper() : db(nullptr), mThreadPool(nullptr) {}

Sqlite3Wrapper::~Sqlite3Wrapper() { close(); }

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

    if (!initializeSchema()) {
        CT::logger.error("数据库表结构初始化失败！");
        return false;
    }

    // 初始化线程池
    if (!mThreadPool) {
        // 使用配置的线程池大小
        mThreadPool = std::make_unique<ThreadPool>(CT::ConfigManager::getInstance().get().databaseThreadPoolSize);
        CT::logger.info("数据库线程池已初始化，线程数: {}", CT::ConfigManager::getInstance().get().databaseThreadPoolSize);
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

bool Sqlite3Wrapper::initializeSchema() {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);

    if (!beginTransaction()) {
        CT::logger.error("无法开始数据库初始化事务。");
        return false;
    }

    // 迁移：检查旧表 locked_chests 是否存在
    std::vector<std::vector<std::string>> tables =
        query_unsafe("SELECT name FROM sqlite_master WHERE type='table' AND name='locked_chests';");
    if (!tables.empty()) {
        CT::logger.info("检测到旧数据表 `locked_chests`，正在迁移...");
        if (execute_unsafe("ALTER TABLE locked_chests RENAME TO chests;")
            && execute_unsafe("ALTER TABLE chests ADD COLUMN type INTEGER DEFAULT 0;")) {
            CT::logger.info("`chests` 数据表迁移成功！");
        } else {
            CT::logger.error("`chests` 数据表迁移失败！");
            rollback();
            return false;
        }
    }

    // 创建所有表
    const char* create_statements[] = {
        "CREATE TABLE IF NOT EXISTS chests ("
        "player_uuid TEXT NOT NULL,"
        "dim_id INTEGER NOT NULL,"
        "pos_x INTEGER NOT NULL,"
        "pos_y INTEGER NOT NULL,"
        "pos_z INTEGER NOT NULL,"
        "type INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z));",

        "CREATE TABLE IF NOT EXISTS shared_chests ("
        "player_uuid TEXT NOT NULL,"
        "owner_uuid TEXT NOT NULL," // 确保 owner_uuid 存在
        "dim_id INTEGER NOT NULL,"
        "pos_x INTEGER NOT NULL,"
        "pos_y INTEGER NOT NULL,"
        "pos_z INTEGER NOT NULL,"
        "PRIMARY KEY (player_uuid, dim_id, pos_x, pos_y, pos_z),"
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE);",

        "CREATE TABLE IF NOT EXISTS item_definitions ("
        "item_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "item_nbt TEXT NOT NULL UNIQUE);",

        "CREATE TABLE IF NOT EXISTS items (id INTEGER PRIMARY KEY, name TEXT, quantity INTEGER);",

        "CREATE TABLE IF NOT EXISTS shop_items ("
        "dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL,"
        "slot INTEGER, item_id INTEGER NOT NULL, price INTEGER NOT NULL, db_count INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_id),"
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE,"
        "FOREIGN KEY (item_id) REFERENCES item_definitions(item_id) ON DELETE CASCADE);",

        "CREATE TABLE IF NOT EXISTS purchase_records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL,"
        "item_id INTEGER NOT NULL, buyer_uuid TEXT NOT NULL, purchase_count INTEGER NOT NULL, total_price INTEGER NOT "
        "NULL,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_id) REFERENCES shop_items(dim_id, pos_x, pos_y, pos_z, "
        "item_id) ON DELETE CASCADE);",

        "CREATE TABLE IF NOT EXISTS recycle_shop_items ("
        "dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL,"
        "item_id INTEGER NOT NULL, price INTEGER NOT NULL, min_durability INTEGER DEFAULT 0,"
        "required_enchants TEXT NOT NULL DEFAULT '',"
        "max_recycle_count INTEGER NOT NULL DEFAULT 0, current_recycled_count INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_id),"
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE,"
        "FOREIGN KEY (item_id) REFERENCES item_definitions(item_id) ON DELETE CASCADE);",

        "CREATE TABLE IF NOT EXISTS recycle_records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL,"
        "item_id INTEGER NOT NULL, recycler_uuid TEXT NOT NULL, recycle_count INTEGER NOT NULL, total_price INTEGER "
        "NOT NULL,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_id) REFERENCES recycle_shop_items(dim_id, pos_x, pos_y, pos_z, "
        "item_id) ON DELETE CASCADE);"
    };

    for (const char* sql : create_statements) {
        if (!execute_unsafe(sql)) {
            // execute_unsafe 内部会打印错误
            rollback();
            return false;
        }
    }

    // --- 数据迁移和表结构调整 ---

    // 迁移：shared_chests.owner_uuid
    if (!isColumnExists("shared_chests", "owner_uuid")) {
        CT::logger.info("为 `shared_chests` 添加 `owner_uuid` 字段...");
        if (!execute_unsafe("ALTER TABLE shared_chests ADD COLUMN owner_uuid TEXT NOT NULL DEFAULT '';")) {
            CT::logger.error("添加 `owner_uuid` 字段失败！");
            rollback();
            return false;
        }
    }

    // 迁移：shop_items 从 item_nbt 到 item_id
    if (isColumnExists("shop_items", "item_nbt") && !isColumnExists("shop_items", "item_id")) {
        CT::logger.info("迁移 `shop_items` 表结构...");
        if (!execute_unsafe(
                "INSERT OR IGNORE INTO item_definitions (item_nbt) SELECT DISTINCT item_nbt FROM shop_items;"
            )
            || !execute_unsafe("ALTER TABLE shop_items RENAME TO shop_items_old;")
            || !execute_unsafe(create_statements[4]) || // Re-create new shop_items table
            !execute_unsafe("INSERT INTO shop_items (dim_id, pos_x, pos_y, pos_z, slot, item_id, price, db_count) "
                            "SELECT o.dim_id, o.pos_x, o.pos_y, o.pos_z, o.slot, d.item_id, o.price, o.db_count "
                            "FROM shop_items_old o JOIN item_definitions d ON o.item_nbt = d.item_nbt;")
            || !execute_unsafe("DROP TABLE shop_items_old;")) {
            CT::logger.error("`shop_items` 表迁移失败！");
            rollback();
            return false;
        }
        CT::logger.info("`shop_items` 表迁移成功！");
    }

    // 迁移：recycle_shop_items 从 item_nbt 到 item_id
    if (isColumnExists("recycle_shop_items", "item_nbt") && !isColumnExists("recycle_shop_items", "item_id")) {
        CT::logger.info("迁移 `recycle_shop_items` 表结构...");
        if (!execute_unsafe(
                "INSERT OR IGNORE INTO item_definitions (item_nbt) SELECT DISTINCT item_nbt FROM recycle_shop_items;"
            )
            || !execute_unsafe("ALTER TABLE recycle_shop_items RENAME TO recycle_shop_items_old;")
            || !execute_unsafe(create_statements[6]) || // Re-create new recycle_shop_items table
            !execute_unsafe(
                "INSERT INTO recycle_shop_items (dim_id, pos_x, pos_y, pos_z, item_id, price, min_durability, "
                "required_enchants, max_recycle_count, current_recycled_count) "
                "SELECT o.dim_id, o.pos_x, o.pos_y, o.pos_z, d.item_id, o.price, o.min_durability, '', 0, 0 "
                "FROM recycle_shop_items_old o JOIN item_definitions d ON o.item_nbt = d.item_nbt;"
            )
            || !execute_unsafe("DROP TABLE recycle_shop_items_old;")) {
            CT::logger.error("`recycle_shop_items` 表迁移失败！");
            rollback();
            return false;
        }
        CT::logger.info("`recycle_shop_items` 表迁移成功！");
    }

    // 迁移：purchase_records 从 item_nbt 到 item_id
    if (isColumnExists("purchase_records", "item_nbt") && !isColumnExists("purchase_records", "item_id")) {
        CT::logger.info("迁移 `purchase_records` 表结构...");
        if (!execute_unsafe("ALTER TABLE purchase_records RENAME TO purchase_records_old;")
            || !execute_unsafe(create_statements[5]) || // Re-create new purchase_records table
            !execute_unsafe("INSERT INTO purchase_records (id, dim_id, pos_x, pos_y, pos_z, item_id, buyer_uuid, "
                            "purchase_count, total_price, timestamp) "
                            "SELECT o.id, o.dim_id, o.pos_x, o.pos_y, o.pos_z, d.item_id, o.buyer_uuid, "
                            "o.purchase_count, o.total_price, o.timestamp "
                            "FROM purchase_records_old o JOIN item_definitions d ON o.item_nbt = d.item_nbt;")
            || !execute_unsafe("DROP TABLE purchase_records_old;")) {
            CT::logger.error("`purchase_records` 表迁移失败！");
            rollback();
            return false;
        }
        CT::logger.info("`purchase_records` 表迁移成功！");
    }

    // 迁移：recycle_records 从 item_nbt 到 item_id
    if (isColumnExists("recycle_records", "item_nbt") && !isColumnExists("recycle_records", "item_id")) {
        CT::logger.info("迁移 `recycle_records` 表结构...");
        if (!execute_unsafe("ALTER TABLE recycle_records RENAME TO recycle_records_old;")
            || !execute_unsafe(create_statements[7]) || // Re-create new recycle_records table
            !execute_unsafe("INSERT INTO recycle_records (id, dim_id, pos_x, pos_y, pos_z, item_id, recycler_uuid, "
                            "recycle_count, total_price, timestamp) "
                            "SELECT o.id, o.dim_id, o.pos_x, o.pos_y, o.pos_z, d.item_id, o.recycler_uuid, "
                            "o.recycle_count, o.total_price, o.timestamp "
                            "FROM recycle_records_old o JOIN item_definitions d ON o.item_nbt = d.item_nbt;")
            || !execute_unsafe("DROP TABLE recycle_records_old;")) {
            CT::logger.error("`recycle_records` 表迁移失败！");
            rollback();
            return false;
        }
        CT::logger.info("`recycle_records` 表迁移成功！");
    }

    // 添加新列
    if (!isColumnExists("shop_items", "db_count")) {
        execute_unsafe("ALTER TABLE shop_items ADD COLUMN db_count INTEGER NOT NULL DEFAULT 0;");
    }
    if (!isColumnExists("recycle_shop_items", "required_enchants")) {
        execute_unsafe("ALTER TABLE recycle_shop_items ADD COLUMN required_enchants TEXT NOT NULL DEFAULT '';");
    }
    if (!isColumnExists("recycle_shop_items", "max_recycle_count")) {
        execute_unsafe("ALTER TABLE recycle_shop_items ADD COLUMN max_recycle_count INTEGER NOT NULL DEFAULT 0;");
    }
    if (!isColumnExists("recycle_shop_items", "current_recycled_count")) {
        execute_unsafe("ALTER TABLE recycle_shop_items ADD COLUMN current_recycled_count INTEGER NOT NULL DEFAULT 0;");
    }

    // 创建索引
    const char* create_indices[] = {
        "CREATE INDEX IF NOT EXISTS idx_chests_position ON chests(dim_id, pos_x, pos_y, pos_z);",
        "CREATE INDEX IF NOT EXISTS idx_shared_chests_position ON shared_chests(dim_id, pos_x, pos_y, pos_z);",
        "CREATE INDEX IF NOT EXISTS idx_shop_items_position ON shop_items(dim_id, pos_x, pos_y, pos_z);"
    };
    for (const char* sql : create_indices) {
        if (!execute_unsafe(sql)) {
            // 索引创建失败通常不是致命错误，记录警告即可
            CT::logger.warn("无法创建索引: {}", sql);
        }
    }

    if (!commit()) {
        CT::logger.error("无法提交数据库初始化事务。");
        return false;
    }

    CT::logger.info("数据库表结构初始化完成。");
    return true;
}

bool Sqlite3Wrapper::execute_unsafe(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    char*                                 err_msg = nullptr;
    if (sqlite3_exec(db, sql.c_str(), 0, 0, &err_msg) != SQLITE_OK) {
        std::cerr << "SQL error: " << err_msg << std::endl;
        sqlite3_free(err_msg);
        return false;
    }
    clearCache(); // 清除缓存，因为数据已更改
    return true;
}

static int callback(void* data, int argc, char** argv, char** azColName) {
    auto*                    results = static_cast<std::vector<std::vector<std::string>>*>(data);
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
    char*                                 err_msg = nullptr;
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
    clearCache(); // 提交后清除缓存
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
    clearCache(); // 回滚后清除缓存
    return true;
}

void Sqlite3Wrapper::clearCache() {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    mQueryCache.clear();
}

void Sqlite3Wrapper::setCacheTimeout(int seconds) { mCacheTimeoutSeconds = seconds; }

void Sqlite3Wrapper::enableCache(bool enable) {
    mCacheEnabled = enable;
    if (!enable) {
        clearCache();
    }
}

bool Sqlite3Wrapper::isColumnExists(const std::string& tableName, const std::string& columnName) {
    if (!db) return false;

    std::string                           sql    = "PRAGMA table_info(" + tableName + ");";
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
        std::visit(
            [&ss](auto&& arg) {
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
            },
            param
        );
    }

    return ss.str();
}

bool Sqlite3Wrapper::getCachedResult(const std::string& key, std::vector<std::vector<std::string>>& result) {
    std::lock_guard<std::mutex> lock(mCacheMutex);

    auto it = mQueryCache.find(key);
    if (it == mQueryCache.end()) {
        return false;
    }

    auto now     = std::chrono::steady_clock::now();
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

    mQueryCache[key] = {result, std::chrono::steady_clock::now()};
}

bool Sqlite3Wrapper::shouldSkipCache(const std::string& sql) {
    // 复制一份sql并转换为小写，用于不区分大小写的比较
    std::string lowerSql = sql;
    std::transform(lowerSql.begin(), lowerSql.end(), lowerSql.begin(), [](unsigned char c) { return std::tolower(c); });

    // 如果查询包含 "chests" 或 "shared_chests"，则跳过缓存
    // 因为这些表的缓存由 ChestCacheManager 更精确地处理
    return lowerSql.find("chests") != std::string::npos || lowerSql.find("shared_chests") != std::string::npos;
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

void Sqlite3Wrapper::setThreadPoolSize(size_t size) { mThreadPoolSize = size; }

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
    const std::vector<std::string>&        sqlStatements,
    const std::vector<std::vector<Value>>& paramsList
) {

    if (!mThreadPool) {
        throw std::runtime_error("Thread pool not initialized. Call open() first.");
    }

    return mThreadPool->enqueue([this, sqlStatements, paramsList]() {
        return this->executeBatch(sqlStatements, paramsList);
    });
}
