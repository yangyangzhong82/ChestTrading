
#include "Sqlite3Wrapper.h"
#include "logger.h"
#include <algorithm>
#include <functional>
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
        CT::logger.error("无法打开数据库: {}", sqlite3_errmsg(db));
        return false;
    }

    // 设置 busy_timeout，避免 "database is locked" 短暂失败
    int busyTimeoutMs = CT::ConfigManager::getInstance().get().busyTimeoutMs;
    if (sqlite3_busy_timeout(db, busyTimeoutMs) != SQLITE_OK) {
        CT::logger.warn("无法设置 busy_timeout: {}", sqlite3_errmsg(db));
    } else {
        CT::logger.info("数据库 busy_timeout 已设置为 {} 毫秒", busyTimeoutMs);
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
        CT::logger.info(
            "数据库线程池已初始化，线程数: {}",
            CT::ConfigManager::getInstance().get().databaseThreadPoolSize
        );
    }

    return true;
}

void Sqlite3Wrapper::close() {
    mClosing = true;

    // 等待所有异步任务完成
    waitForAllAsyncTasks();

    // 销毁线程池
    mThreadPool.reset();

    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
    clearCache();

    mClosing = false;
}

bool Sqlite3Wrapper::initializeSchema() {
    int currentVersion = getSchemaVersion();
    CT::logger.info("当前数据库 schema 版本: {}", currentVersion);

    if (!runMigrations(currentVersion)) {
        CT::logger.error("数据库迁移失败！");
        return false;
    }

    CT::logger.info("数据库表结构初始化完成，当前版本: {}", getSchemaVersion());
    return true;
}

int Sqlite3Wrapper::getSchemaVersion() {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    auto                                  result = query_unsafe("PRAGMA user_version;");
    if (!result.empty() && !result[0].empty()) {
        return std::stoi(result[0][0]);
    }
    return 0;
}

void Sqlite3Wrapper::setSchemaVersion(int version) {
    execute_unsafe("PRAGMA user_version = " + std::to_string(version) + ";");
}

bool Sqlite3Wrapper::runMigrations(int fromVersion) {
    using MigrationFunc                   = std::function<bool()>;
    std::vector<MigrationFunc> migrations = {
        [this]() { return migrateToV1(); },
        [this]() { return migrateToV2(); },
    };

    for (int v = fromVersion; v < static_cast<int>(migrations.size()); ++v) {
        CT::logger.info("执行迁移: V{} -> V{}", v, v + 1);
        Transaction txn(*this);
        if (!txn.isActive()) {
            CT::logger.error("无法开始迁移事务 V{}", v + 1);
            return false;
        }

        if (!migrations[v]()) {
            CT::logger.error("迁移到 V{} 失败！", v + 1);
            return false;
        }

        setSchemaVersion(v + 1);
        if (!txn.commit()) {
            CT::logger.error("无法提交迁移事务 V{}", v + 1);
            return false;
        }
        CT::logger.info("迁移到 V{} 成功", v + 1);
    }
    return true;
}

bool Sqlite3Wrapper::migrateToV1() {
    const char* sqls[] = {
        "CREATE TABLE IF NOT EXISTS chests ("
        "player_uuid TEXT NOT NULL, dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, "
        "pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL, type INTEGER NOT NULL DEFAULT 0, "
        "shop_name TEXT NOT NULL DEFAULT '', enable_floating_text INTEGER NOT NULL DEFAULT 1, "
        "enable_fake_item INTEGER NOT NULL DEFAULT 1, is_public INTEGER NOT NULL DEFAULT 1, "
        "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z));",

        "CREATE TABLE IF NOT EXISTS shared_chests ("
        "player_uuid TEXT NOT NULL, owner_uuid TEXT NOT NULL, dim_id INTEGER NOT NULL, "
        "pos_x INTEGER NOT NULL, pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL, "
        "PRIMARY KEY (player_uuid, dim_id, pos_x, pos_y, pos_z), "
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE);",

        "CREATE TABLE IF NOT EXISTS item_definitions (item_id INTEGER PRIMARY KEY AUTOINCREMENT, item_nbt TEXT NOT "
        "NULL UNIQUE);",
        "CREATE TABLE IF NOT EXISTS items (id INTEGER PRIMARY KEY, name TEXT, quantity INTEGER);",

        "CREATE TABLE IF NOT EXISTS shop_items ("
        "dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL, "
        "slot INTEGER, item_id INTEGER NOT NULL, price REAL NOT NULL, db_count INTEGER NOT NULL DEFAULT 0, "
        "PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_id), "
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE, "
        "FOREIGN KEY (item_id) REFERENCES item_definitions(item_id) ON DELETE CASCADE);",

        "CREATE TABLE IF NOT EXISTS purchase_records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, "
        "pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL, item_id INTEGER NOT NULL, buyer_uuid TEXT NOT NULL, "
        "purchase_count INTEGER NOT NULL, total_price REAL NOT NULL, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_id) REFERENCES shop_items(dim_id, pos_x, pos_y, pos_z, "
        "item_id) ON DELETE CASCADE);",

        "CREATE TABLE IF NOT EXISTS recycle_shop_items ("
        "dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL, "
        "item_id INTEGER NOT NULL, price REAL NOT NULL, min_durability INTEGER DEFAULT 0, "
        "required_enchants TEXT NOT NULL DEFAULT '', max_recycle_count INTEGER NOT NULL DEFAULT 0, "
        "current_recycled_count INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (dim_id, pos_x, pos_y, pos_z, item_id), "
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z) REFERENCES chests(dim_id, pos_x, pos_y, pos_z) ON DELETE CASCADE, "
        "FOREIGN KEY (item_id) REFERENCES item_definitions(item_id) ON DELETE CASCADE);",

        "CREATE TABLE IF NOT EXISTS recycle_records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, dim_id INTEGER NOT NULL, pos_x INTEGER NOT NULL, "
        "pos_y INTEGER NOT NULL, pos_z INTEGER NOT NULL, item_id INTEGER NOT NULL, recycler_uuid TEXT NOT NULL, "
        "recycle_count INTEGER NOT NULL, total_price REAL NOT NULL, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, "
        "FOREIGN KEY (dim_id, pos_x, pos_y, pos_z, item_id) REFERENCES recycle_shop_items(dim_id, pos_x, pos_y, pos_z, "
        "item_id) ON DELETE CASCADE);",

        "CREATE INDEX IF NOT EXISTS idx_chests_position ON chests(dim_id, pos_x, pos_y, pos_z);",
        "CREATE INDEX IF NOT EXISTS idx_shared_chests_position ON shared_chests(dim_id, pos_x, pos_y, pos_z);",
        "CREATE INDEX IF NOT EXISTS idx_shop_items_position ON shop_items(dim_id, pos_x, pos_y, pos_z);"
    };

    for (const char* sql : sqls) {
        if (!execute_unsafe(sql)) return false;
    }
    return true;
}

bool Sqlite3Wrapper::migrateToV2() {
    const char* sqls[] = {
        // chests 表索引 - 优化按玩家查询和按玩家+类型统计
        "CREATE INDEX IF NOT EXISTS idx_chests_player_uuid ON chests(player_uuid);",
        "CREATE INDEX IF NOT EXISTS idx_chests_player_type ON chests(player_uuid, type);",

        // shared_chests 表索引 - 优化按玩家查询被分享的箱子
        "CREATE INDEX IF NOT EXISTS idx_shared_chests_player ON shared_chests(player_uuid);",

        // shop_items 表索引 - 优化 JOIN 和 item_id 查询
        "CREATE INDEX IF NOT EXISTS idx_shop_items_item_id ON shop_items(item_id);",

        // purchase_records 表索引 - 优化按位置和时间查询记录
        "CREATE INDEX IF NOT EXISTS idx_purchase_records_position ON purchase_records(dim_id, pos_x, pos_y, pos_z);",
        "CREATE INDEX IF NOT EXISTS idx_purchase_records_timestamp ON purchase_records(timestamp DESC);",
        "CREATE INDEX IF NOT EXISTS idx_purchase_records_buyer ON purchase_records(buyer_uuid);",

        // recycle_shop_items 表索引 - 优化按位置查询
        "CREATE INDEX IF NOT EXISTS idx_recycle_shop_items_position ON recycle_shop_items(dim_id, pos_x, pos_y, "
        "pos_z);",
        "CREATE INDEX IF NOT EXISTS idx_recycle_shop_items_item_id ON recycle_shop_items(item_id);",

        // recycle_records 表索引 - 优化按位置、item_id 和时间查询
        "CREATE INDEX IF NOT EXISTS idx_recycle_records_position ON recycle_records(dim_id, pos_x, pos_y, pos_z);",
        "CREATE INDEX IF NOT EXISTS idx_recycle_records_item ON recycle_records(dim_id, pos_x, pos_y, pos_z, item_id);",
        "CREATE INDEX IF NOT EXISTS idx_recycle_records_timestamp ON recycle_records(timestamp DESC);",
        "CREATE INDEX IF NOT EXISTS idx_recycle_records_recycler ON recycle_records(recycler_uuid);"
    };

    for (const char* sql : sqls) {
        if (!execute_unsafe(sql)) return false;
    }
    return true;
}

bool Sqlite3Wrapper::execute_unsafe(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    char*                                 err_msg = nullptr;
    if (sqlite3_exec(db, sql.c_str(), 0, 0, &err_msg) != SQLITE_OK) {
        CT::logger.error("SQL 错误: {}", err_msg);
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
        CT::logger.error("SQL 错误: {}", err_msg);
        sqlite3_free(err_msg);
    }
    return results;
}

// === Transaction RAII 类实现 ===

Transaction::Transaction(Sqlite3Wrapper& db) : mDb(db), mLock(db.mDbMutex) {
    if (!mDb.db) return;

    char* err_msg = nullptr;
    if (sqlite3_exec(mDb.db, "BEGIN TRANSACTION;", nullptr, nullptr, &err_msg) == SQLITE_OK) {
        mActive = true;
        mDb.mStats.transactionCount++;
    } else {
        CT::logger.error("无法开始事务: {}", err_msg ? err_msg : "未知错误");
        if (err_msg) sqlite3_free(err_msg);
    }
}

Transaction::~Transaction() {
    if (mActive && !mCommitted) {
        rollback();
    }
}

bool Transaction::commit() {
    if (!mActive || mCommitted) return false;

    char* err_msg = nullptr;
    if (sqlite3_exec(mDb.db, "COMMIT;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        CT::logger.error("无法提交事务: {}", err_msg ? err_msg : "未知错误");
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }

    mCommitted = true;
    mActive    = false;
    mDb.clearCache();
    return true;
}

void Transaction::rollback() {
    if (!mActive) return;

    char* err_msg = nullptr;
    if (sqlite3_exec(mDb.db, "ROLLBACK;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        CT::logger.error("无法回滚事务: {}", err_msg ? err_msg : "未知错误");
        if (err_msg) sqlite3_free(err_msg);
    }

    mActive = false;
    mDb.clearCache();
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
    std::string lowerSql = sql;
    std::transform(lowerSql.begin(), lowerSql.end(), lowerSql.begin(), [](unsigned char c) { return std::tolower(c); });
    return lowerSql.find("chests") != std::string::npos || lowerSql.find("shared_chests") != std::string::npos;
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
    if (mClosing) {
        std::promise<bool> p;
        p.set_value(false);
        return p.get_future();
    }

    return mThreadPool->enqueue([this, sqlStatements, paramsList]() {
        return this->executeBatch(sqlStatements, paramsList);
    });
}
