
#include "Sqlite3Wrapper.h"
#include "SchemaMigration.h"
#include "logger.h"
#include <algorithm>
#include <iomanip>
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

    // 执行 Schema 迁移
    if (!SchemaMigration::run(*this)) {
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

    // 初始化缓存超时时间
    setCacheTimeout(CT::ConfigManager::getInstance().get().databaseCacheTimeoutSec);

    return true;
}

void Sqlite3Wrapper::close() {
    mClosing = true;

    // 等待所有异步任务完成
    waitForAllAsyncTasks();

    // 销毁线程池
    mThreadPool.reset();

    std::lock_guard<std::recursive_mutex> lock(mDbMutex);

    // 清理预处理语句缓存
    clearStmtCache();

    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
    clearCache();

    mClosing = false;
}

sqlite3_stmt* Sqlite3Wrapper::getOrPrepareStmt(const std::string& sql) {
    auto it = mStmtCache.find(sql);
    if (it != mStmtCache.end()) {
        return it->second;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        CT::logger.error("无法准备 SQL 语句: {}", sqlite3_errmsg(db));
        return nullptr;
    }

    mStmtCache[sql] = stmt;
    return stmt;
}

void Sqlite3Wrapper::clearStmtCache() {
    for (auto& [sql, stmt] : mStmtCache) {
        if (stmt) sqlite3_finalize(stmt);
    }
    mStmtCache.clear();
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

void Sqlite3Wrapper::clearCacheForTable(const std::string& tableName) {
    if (tableName.empty()) {
        clearCache();
        return;
    }
    std::lock_guard<std::mutex> lock(mCacheMutex);
    for (auto it = mQueryCache.begin(); it != mQueryCache.end();) {
        if (it->first.find(tableName) != std::string::npos) {
            it = mQueryCache.erase(it);
        } else {
            ++it;
        }
    }
}

std::string Sqlite3Wrapper::extractTableName(const std::string& sql) {
    std::string lowerSql = sql;
    std::transform(lowerSql.begin(), lowerSql.end(), lowerSql.begin(), [](unsigned char c) { return std::tolower(c); });

    // INSERT INTO table_name
    auto pos = lowerSql.find("insert into ");
    if (pos != std::string::npos) {
        pos      += 12;
        auto end  = lowerSql.find_first_of(" (", pos);
        return sql.substr(pos, end - pos);
    }
    // UPDATE table_name
    pos = lowerSql.find("update ");
    if (pos != std::string::npos) {
        pos      += 7;
        auto end  = lowerSql.find_first_of(" ", pos);
        return sql.substr(pos, end - pos);
    }
    // DELETE FROM table_name
    pos = lowerSql.find("delete from ");
    if (pos != std::string::npos) {
        pos      += 12;
        auto end  = lowerSql.find_first_of(" ", pos);
        return sql.substr(pos, end - pos);
    }
    return "";
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
    std::vector<std::vector<std::string>> result = query(sql);

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
