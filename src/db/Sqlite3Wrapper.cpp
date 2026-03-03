#include "Sqlite3Wrapper.h"
#include "SchemaMigration.h"
#include "logger.h"
#include <algorithm>
#include <filesystem>
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

    mDbPath = db_path; // 保存路径用于读连接池

    // 确保数据库目录存在，避免 sqlite3_open 因父目录缺失失败。
    try {
        std::filesystem::path dbFilePath(db_path);
        if (dbFilePath.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(dbFilePath.parent_path(), ec);
            if (ec) {
                CT::logger.error(
                    "无法创建数据库目录: {} ({})",
                    dbFilePath.parent_path().string(),
                    ec.message()
                );
                return false;
            }
        }
    } catch (const std::exception& e) {
        CT::logger.error("处理数据库路径时发生异常: {}", e.what());
        return false;
    }

    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        CT::logger.error("无法打开数据库 {}: {}", db_path, db ? sqlite3_errmsg(db) : "未知错误");
        if (db) {
            sqlite3_close(db);
            db = nullptr;
        }
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
        mThreadPool = std::make_unique<ThreadPool>(CT::ConfigManager::getInstance().get().databaseThreadPoolSize);
        CT::logger.info(
            "数据库线程池已初始化，线程数: {}",
            CT::ConfigManager::getInstance().get().databaseThreadPoolSize
        );
    }

    // 初始化读连接池（仅在 WAL 模式下有效）
    if (CT::ConfigManager::getInstance().get().enableWalMode) {
        mReadConnPoolSize = CT::ConfigManager::getInstance().get().databaseThreadPoolSize;
        if (initReadConnPool()) {
            CT::logger.info("读连接池已初始化，连接数: {}", mReadConnPoolSize);
        }
    }

    // 初始化缓存超时时间
    mQueryCache.setTimeout(CT::ConfigManager::getInstance().get().databaseCacheTimeoutSec);

    return true;
}

void Sqlite3Wrapper::close() {
    mClosing = true;

    // 先关闭读连接池（唤醒所有等待连接的线程）
    closeReadConnPool();

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
    mQueryCache.clear();

    mClosing = false;
}

// === 读连接池实现 ===

bool Sqlite3Wrapper::initReadConnPool() {
    std::lock_guard<std::mutex> lock(mReadConnMutex);

    int busyTimeoutMs = CT::ConfigManager::getInstance().get().busyTimeoutMs;

    for (size_t i = 0; i < mReadConnPoolSize; ++i) {
        sqlite3* conn = nullptr;
        if (sqlite3_open_v2(mDbPath.c_str(), &conn, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            CT::logger.error("无法创建读连接 {}: {}", i, conn ? sqlite3_errmsg(conn) : "未知错误");
            if (conn) sqlite3_close(conn);
            continue;
        }
        sqlite3_busy_timeout(conn, busyTimeoutMs);
        mReadConnPool.push(conn);
    }

    return !mReadConnPool.empty();
}

void Sqlite3Wrapper::closeReadConnPool() {
    {
        std::unique_lock<std::mutex> lock(mReadConnMutex);

        // 等待所有活跃连接归还
        mReadConnCond.wait(lock, [this] { return mActiveReadConns.load() == 0; });

        while (!mReadConnPool.empty()) {
            sqlite3* conn = mReadConnPool.front();
            mReadConnPool.pop();
            if (conn) sqlite3_close(conn);
        }
    }
    // 在锁外通知所有等待的线程
    mReadConnCond.notify_all();
}

sqlite3* Sqlite3Wrapper::acquireReadConn() {
    std::unique_lock<std::mutex> lock(mReadConnMutex);

    // 循环等待可用连接或关闭信号，超时 5 秒
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!mClosing && mReadConnPool.empty()) {
        if (mReadConnCond.wait_until(lock, deadline) == std::cv_status::timeout) {
            return nullptr;
        }
    }

    if (mClosing || mReadConnPool.empty()) {
        return nullptr;
    }

    sqlite3* conn = mReadConnPool.front();
    mReadConnPool.pop();
    mActiveReadConns++;
    return conn;
}

void Sqlite3Wrapper::releaseReadConn(sqlite3* conn) {
    if (!conn) return;

    {
        std::lock_guard<std::mutex> lock(mReadConnMutex);
        mReadConnPool.push(conn);
        mActiveReadConns--;
    }
    mReadConnCond.notify_one();
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
        mDb.mCurrentTransaction = this;
    } else {
        CT::logger.error("无法开始事务: {}", err_msg ? err_msg : "未知错误");
        if (err_msg) sqlite3_free(err_msg);
    }
}

Transaction::~Transaction() {
    if (mActive && !mCommitted) {
        rollback();
    }
    if (mDb.mCurrentTransaction == this) {
        mDb.mCurrentTransaction = nullptr;
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

    mCommitted              = true;
    mActive                 = false;
    mDb.mCurrentTransaction = nullptr;
    // 只清除受影响表的缓存
    for (const auto& table : mAffectedTables) {
        mDb.mQueryCache.clearForTable(table);
    }
    return true;
}

void Transaction::rollback() {
    if (!mActive) return;

    char* err_msg = nullptr;
    if (sqlite3_exec(mDb.db, "ROLLBACK;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
        CT::logger.error("无法回滚事务: {}", err_msg ? err_msg : "未知错误");
        if (err_msg) sqlite3_free(err_msg);
    }

    mActive                 = false;
    mDb.mCurrentTransaction = nullptr;
}

void Transaction::markTableAffected(const std::string& tableName) {
    if (!tableName.empty()) {
        mAffectedTables.insert(tableName);
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
