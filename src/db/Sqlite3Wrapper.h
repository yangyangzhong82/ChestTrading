#pragma once

#include "ThreadPool.h"
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <sqlite3.h>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>


class Sqlite3Wrapper;

// Transaction RAII 类 - 在构造时加锁并 BEGIN，析构时自动 ROLLBACK（如果未提交）
class Transaction {
public:
    explicit Transaction(Sqlite3Wrapper& db);
    ~Transaction();

    Transaction(const Transaction&)            = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&)                 = delete;
    Transaction& operator=(Transaction&&)      = delete;

    bool commit();
    void rollback();
    bool isActive() const { return mActive; }

private:
    Sqlite3Wrapper&                        mDb;
    std::unique_lock<std::recursive_mutex> mLock;
    bool                                   mActive    = false;
    bool                                   mCommitted = false;
};

class Sqlite3Wrapper {
public:
    friend class Transaction;

    // 获取单例实例
    static Sqlite3Wrapper& getInstance() {
        static Sqlite3Wrapper instance;
        return instance;
    }

    // 删除拷贝构造函数和赋值运算符
    Sqlite3Wrapper(const Sqlite3Wrapper&)            = delete;
    Sqlite3Wrapper& operator=(const Sqlite3Wrapper&) = delete;

    // 定义可以绑定到SQL语句的参数类型
    using Value = std::variant<int, long long, double, std::string, const char*>;

    bool open(const std::string& db_path);
    void close();

    // 不安全的旧方法,建议不再使用
    bool                                  execute_unsafe(const std::string& sql);
    std::vector<std::vector<std::string>> query_unsafe(const std::string& sql);

    // 支持参数绑定的安全 execute 方法
    template <typename... Args>
    bool execute(const std::string& sql, Args&&... args);

    // 执行并返回受影响的行数（用于 UPDATE/DELETE 等需要检查实际更新行数的场景）
    template <typename... Args>
    int executeAndGetChanges(const std::string& sql, Args&&... args);

    // 支持参数绑定的安全 query 方法
    template <typename... Args>
    std::vector<std::vector<std::string>> query(const std::string& sql, Args&&... args);


    // 批量操作(自动使用事务)
    template <typename... Args>
    bool executeBatch(const std::vector<std::string>& sqlStatements, const std::vector<std::vector<Value>>& paramsList);

    // 缓存控制
    void clearCache();
    void setCacheTimeout(int seconds);
    void enableCache(bool enable);

    // 检查列是否存在
    bool isColumnExists(const std::string& tableName, const std::string& columnName);

    // 获取或创建 item_id（用于 item_definitions 表）
    int getOrCreateItemId(const std::string& itemNbt);

    // 根据 item_id 获取 item_nbt
    std::string getItemNbtById(int itemId);

    // === 异步操作接口 ===

    // 异步执行 SQL 语句
    template <typename... Args>
    std::future<bool> executeAsync(const std::string& sql, Args&&... args);

    // 异步查询
    template <typename... Args>
    std::future<std::vector<std::vector<std::string>>> queryAsync(const std::string& sql, Args&&... args);

    // 异步批量操作
    std::future<bool>
    executeBatchAsync(const std::vector<std::string>& sqlStatements, const std::vector<std::vector<Value>>& paramsList);

    // 设置线程池大小（需要在 open 之前调用）
    void setThreadPoolSize(size_t size);

    // 获取线程池待处理任务数
    size_t getPendingAsyncTasks() const;

    // 等待所有异步任务完成
    void waitForAllAsyncTasks();

    // 获取数据库统计信息
    struct DbStats {
        int cacheHits        = 0;
        int cacheMisses      = 0;
        int queryCount       = 0;
        int transactionCount = 0;
    };
    DbStats getStats() const { return mStats; }

private:
    Sqlite3Wrapper();
    ~Sqlite3Wrapper();

    // 初始化数据库表结构
    bool initializeSchema();

    sqlite3*             db;
    std::recursive_mutex mDbMutex; // 数据库操作互斥锁

    // 查询缓存相关
    struct CacheEntry {
        std::vector<std::vector<std::string>> result;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::unordered_map<std::string, CacheEntry> mQueryCache;
    std::mutex                                  mCacheMutex;
    int                                         mCacheTimeoutSeconds = 60; // 默认缓存60秒
    bool                                        mCacheEnabled        = true;

    // 统计信息
    mutable DbStats mStats;

    // 线程池相关
    std::unique_ptr<ThreadPool> mThreadPool;
    size_t                      mThreadPoolSize = 4; // 默认4个线程

    // 内部辅助函数,用于绑定参数
    template <size_t I = 1, typename T, typename... Args>
    void bind_args(sqlite3_stmt* stmt, T&& value, Args&&... args);

    template <size_t I = 1>
    void bind_args(sqlite3_stmt* stmt) {} // 递归终止

    // 生成缓存键
    std::string generateCacheKey(const std::string& sql, const std::vector<Value>& params);

    // 从缓存获取结果
    bool getCachedResult(const std::string& key, std::vector<std::vector<std::string>>& result);

    // 存储结果到缓存
    void setCachedResult(const std::string& key, const std::vector<std::vector<std::string>>& result);

    // 判断查询是否应跳过缓存
    bool shouldSkipCache(const std::string& sql);

    // 执行准备好的语句
    template <typename... Args>
    bool executeStatement(sqlite3_stmt* stmt, Args&&... args);

    // 查询准备好的语句
    template <typename... Args>
    std::vector<std::vector<std::string>> queryStatement(sqlite3_stmt* stmt, Args&&... args);
};


// --- 模板函数的实现需要放在头文件中 ---

template <typename... Args>
bool Sqlite3Wrapper::execute(const std::string& sql, Args&&... args) {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);

    if (!db) return false;

    mStats.queryCount++;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }

    bind_args(stmt, std::forward<Args>(args)...);

    bool result = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!result) {
        std::cerr << "Failed to execute statement: " << sqlite3_errmsg(db) << std::endl;
    }

    sqlite3_finalize(stmt);

    if (result) {
        clearCache(); // 成功执行后清除缓存
    }

    return result;
}

template <typename... Args>
int Sqlite3Wrapper::executeAndGetChanges(const std::string& sql, Args&&... args) {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);

    if (!db) return -1;

    mStats.queryCount++;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return -1;
    }

    bind_args(stmt, std::forward<Args>(args)...);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    int  changes = success ? sqlite3_changes(db) : -1;

    sqlite3_finalize(stmt);

    if (success) {
        clearCache();
    }

    return changes;
}

template <typename... Args>
std::vector<std::vector<std::string>> Sqlite3Wrapper::query(const std::string& sql, Args&&... args) {
    std::vector<std::vector<std::string>> results;

    const bool  skipCache = shouldSkipCache(sql);
    std::string cacheKey;

    if (mCacheEnabled && !skipCache) {
        std::vector<Value> params;
        if constexpr (sizeof...(args) > 0) {
            params = {Value(std::forward<Args>(args))...};
        }
        cacheKey = generateCacheKey(sql, params);

        if (getCachedResult(cacheKey, results)) {
            mStats.cacheHits++;
            return results;
        }
    }

    mStats.cacheMisses++;

    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    if (!db) return results;

    mStats.queryCount++;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        return results;
    }

    bind_args(stmt, std::forward<Args>(args)...);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::vector<std::string> row;
        int                      col_count = sqlite3_column_count(stmt);
        for (int i = 0; i < col_count; ++i) {
            const char* col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            row.push_back(col_text ? col_text : "NULL");
        }
        results.push_back(row);
    }

    sqlite3_finalize(stmt);

    if (mCacheEnabled && !skipCache) {
        setCachedResult(cacheKey, results);
    }

    return results;
}

template <size_t I, typename T, typename... Args>
void Sqlite3Wrapper::bind_args(sqlite3_stmt* stmt, T&& value, Args&&... args) {
    // 根据类型绑定参数
    if constexpr (std::is_same_v<std::decay_t<T>, int>) {
        sqlite3_bind_int(stmt, I, value);
    } else if constexpr (std::is_same_v<std::decay_t<T>, long long>) {
        sqlite3_bind_int64(stmt, I, value);
    } else if constexpr (std::is_same_v<std::decay_t<T>, double>) {
        sqlite3_bind_double(stmt, I, value);
    } else if constexpr (std::is_same_v<std::decay_t<T>, std::string>) {
        sqlite3_bind_text(stmt, I, value.c_str(), -1, SQLITE_TRANSIENT);
    } else if constexpr (std::is_same_v<std::decay_t<T>, const char*>) {
        sqlite3_bind_text(stmt, I, value, -1, SQLITE_STATIC);
    }

    // 递归绑定下一个参数
    if constexpr (sizeof...(args) > 0) {
        bind_args<I + 1>(stmt, std::forward<Args>(args)...);
    }
}

template <typename... Args>
bool Sqlite3Wrapper::executeBatch(
    const std::vector<std::string>&        sqlStatements,
    const std::vector<std::vector<Value>>& paramsList
) {
    if (sqlStatements.size() != paramsList.size()) {
        std::cerr << "SQL statements count doesn't match parameters count" << std::endl;
        return false;
    }

    // 使用 Transaction RAII 类
    Transaction txn(*this);
    if (!txn.isActive()) {
        return false;
    }

    for (size_t i = 0; i < sqlStatements.size(); ++i) {
        const auto& sql    = sqlStatements[i];
        const auto& params = paramsList[i];

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Failed to prepare batch statement: " << sqlite3_errmsg(db) << std::endl;
            return false; // txn 析构时自动 rollback
        }

        // 绑定参数
        for (size_t j = 0; j < params.size(); ++j) {
            std::visit(
                [&](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, int>) {
                        sqlite3_bind_int(stmt, j + 1, arg);
                    } else if constexpr (std::is_same_v<T, long long>) {
                        sqlite3_bind_int64(stmt, j + 1, arg);
                    } else if constexpr (std::is_same_v<T, double>) {
                        sqlite3_bind_double(stmt, j + 1, arg);
                    } else if constexpr (std::is_same_v<T, std::string>) {
                        sqlite3_bind_text(stmt, j + 1, arg.c_str(), -1, SQLITE_TRANSIENT);
                    } else if constexpr (std::is_same_v<T, const char*>) {
                        sqlite3_bind_text(stmt, j + 1, arg, -1, SQLITE_STATIC);
                    }
                },
                params[j]
            );
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Failed to execute batch statement: " << sqlite3_errmsg(db) << std::endl;
            sqlite3_finalize(stmt);
            return false; // txn 析构时自动 rollback
        }

        sqlite3_finalize(stmt);
    }

    return txn.commit();
}

// === 异步操作模板实现 ===

template <typename... Args>
std::future<bool> Sqlite3Wrapper::executeAsync(const std::string& sql, Args&&... args) {
    if (!mThreadPool) {
        throw std::runtime_error("Thread pool not initialized. Call open() first.");
    }

    // 捕获参数的副本，避免悬空引用
    auto params = std::make_tuple(std::forward<Args>(args)...);

    return mThreadPool->enqueue([this, sql, params = std::move(params)]() {
        return std::apply(
            [this, &sql](auto&&... args) { return this->execute(sql, std::forward<decltype(args)>(args)...); },
            params
        );
    });
}

template <typename... Args>
std::future<std::vector<std::vector<std::string>>> Sqlite3Wrapper::queryAsync(const std::string& sql, Args&&... args) {
    if (!mThreadPool) {
        throw std::runtime_error("Thread pool not initialized. Call open() first.");
    }

    // 捕获参数的副本
    auto params = std::make_tuple(std::forward<Args>(args)...);

    return mThreadPool->enqueue([this, sql, params = std::move(params)]() {
        return std::apply(
            [this, &sql](auto&&... args) { return this->query(sql, std::forward<decltype(args)>(args)...); },
            params
        );
    });
}
