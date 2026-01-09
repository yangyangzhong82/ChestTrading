#pragma once

#include "ThreadPool.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "logger.h"
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <sqlite3.h>
#include <string>
#include <string_view>
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
    friend class SchemaMigration;

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
    void clearCacheForTable(const std::string& tableName);
    void setCacheTimeout(int seconds);
    void enableCache(bool enable);

    // 检查列是否存在
    bool isColumnExists(const std::string& tableName, const std::string& columnName);

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

    // 在线程池中等待 future 完成，然后在主线程执行回调
    // 用于替代 std::thread([]{future.get(); callback();}).detach() 模式
    template <typename T, typename Callback>
    void thenOnMainThread(std::future<T> fut, Callback&& callback);

    // 在线程池中等待两个 future 完成，然后在主线程执行回调
    template <typename T1, typename T2, typename Callback>
    void thenOnMainThread(std::future<T1> fut1, std::future<T2> fut2, Callback&& callback);

    // 获取数据库统计信息
    struct DbStats {
        std::atomic<int> cacheHits{0};
        std::atomic<int> cacheMisses{0};
        std::atomic<int> queryCount{0};
        std::atomic<int> transactionCount{0};
    };
    struct DbStatsSnapshot {
        int cacheHits;
        int cacheMisses;
        int queryCount;
        int transactionCount;
    };
    DbStatsSnapshot getStats() const {
        return {
            mStats.cacheHits.load(),
            mStats.cacheMisses.load(),
            mStats.queryCount.load(),
            mStats.transactionCount.load()
        };
    }

private:
    Sqlite3Wrapper();
    ~Sqlite3Wrapper();

    sqlite3*             db;
    std::recursive_mutex mDbMutex; // 数据库操作互斥锁

    // 预处理语句缓存
    std::unordered_map<std::string, sqlite3_stmt*> mStmtCache;

    // 查询缓存相关
    struct CacheEntry {
        std::string                           sql; // 用于哈希冲突验证
        std::vector<std::vector<std::string>> result;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::unordered_map<size_t, CacheEntry> mQueryCache;
    std::mutex                             mCacheMutex;
    int                                    mCacheTimeoutSeconds = 60; // 默认缓存60秒
    bool                                   mCacheEnabled        = true;

    // 统计信息
    mutable DbStats mStats;

    // 线程池相关
    std::unique_ptr<ThreadPool> mThreadPool;
    size_t                      mThreadPoolSize = 4; // 默认4个线程
    std::atomic<bool>           mClosing{false};     // 关闭状态标志

    // 内部辅助函数,用于绑定参数
    template <size_t I = 1, typename T, typename... Args>
    void bind_args(sqlite3_stmt* stmt, T&& value, Args&&... args);

    template <size_t I = 1>
    void bind_args(sqlite3_stmt* stmt) {} // 递归终止

    // 获取或创建预处理语句（带缓存）
    sqlite3_stmt* getOrPrepareStmt(const std::string& sql);

    // 清理预处理语句缓存
    void clearStmtCache();

    // 生成缓存键（高效版本）
    template <typename... Args>
    size_t generateCacheKeyFast(const std::string& sql, Args&&... args);

    // 生成缓存键
    size_t generateCacheKey(const std::string& sql, const std::vector<Value>& params);

    // 从缓存获取结果
    bool getCachedResult(size_t key, const std::string& sql, std::vector<std::vector<std::string>>& result);

    // 存储结果到缓存
    void setCachedResult(size_t key, const std::string& sql, const std::vector<std::vector<std::string>>& result);

    // 判断查询是否应跳过缓存
    bool shouldSkipCache(const std::string& sql);

    // 从 SQL 语句中提取表名
    std::string extractTableName(const std::string& sql);

    // 执行准备好的语句
    template <typename... Args>
    bool executeStatement(sqlite3_stmt* stmt, Args&&... args);

    // 查询准备好的语句
    template <typename... Args>
    std::vector<std::vector<std::string>> queryStatement(sqlite3_stmt* stmt, Args&&... args);
};


// --- 模板函数的实现需要放在头文件中 ---

// 高效缓存键生成辅助 - 使用哈希代替字符串拼接
namespace detail {
inline void   hashCombine(size_t& seed, size_t h) { seed ^= h + 0x9e3779b9 + (seed << 6) + (seed >> 2); }
inline size_t hashValue(int v) { return std::hash<int>{}(v); }
inline size_t hashValue(long long v) { return std::hash<long long>{}(v); }
inline size_t hashValue(double v) { return std::hash<double>{}(v); }
inline size_t hashValue(const std::string& v) { return std::hash<std::string>{}(v); }
inline size_t hashValue(const char* v) { return std::hash<std::string_view>{}(v); }
} // namespace detail

template <typename... Args>
size_t Sqlite3Wrapper::generateCacheKeyFast(const std::string& sql, Args&&... args) {
    size_t hash = std::hash<std::string>{}(sql);
    if constexpr (sizeof...(args) > 0) {
        (detail::hashCombine(hash, detail::hashValue(args)), ...);
    }
    return hash;
}

template <typename... Args>
bool Sqlite3Wrapper::execute(const std::string& sql, Args&&... args) {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);

    if (!db) return false;

    mStats.queryCount++;

    sqlite3_stmt* stmt = getOrPrepareStmt(sql);
    if (!stmt) return false;

    bind_args(stmt, std::forward<Args>(args)...);

    bool result = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!result) {
        CT::logger.error("无法执行 SQL 语句: {}", sqlite3_errmsg(db));
    }

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

    if (result) {
        clearCacheForTable(extractTableName(sql));
    }

    return result;
}

template <typename... Args>
int Sqlite3Wrapper::executeAndGetChanges(const std::string& sql, Args&&... args) {
    std::lock_guard<std::recursive_mutex> lock(mDbMutex);

    if (!db) return -1;

    mStats.queryCount++;

    sqlite3_stmt* stmt = getOrPrepareStmt(sql);
    if (!stmt) return -1;

    bind_args(stmt, std::forward<Args>(args)...);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    int  changes = success ? sqlite3_changes(db) : -1;

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

    if (success) {
        clearCacheForTable(extractTableName(sql));
    }

    return changes;
}

template <typename... Args>
std::vector<std::vector<std::string>> Sqlite3Wrapper::query(const std::string& sql, Args&&... args) {
    std::vector<std::vector<std::string>> results;

    const bool skipCache = shouldSkipCache(sql);
    size_t     cacheKey  = 0;

    if (mCacheEnabled && !skipCache) {
        cacheKey = generateCacheKeyFast(sql, args...);
        if (getCachedResult(cacheKey, sql, results)) {
            mStats.cacheHits++;
            return results;
        }
    }

    mStats.cacheMisses++;

    std::lock_guard<std::recursive_mutex> lock(mDbMutex);
    if (!db) return results;

    mStats.queryCount++;

    sqlite3_stmt* stmt = getOrPrepareStmt(sql);
    if (!stmt) return results;

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

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

    if (mCacheEnabled && !skipCache) {
        setCachedResult(cacheKey, sql, results);
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
        CT::logger.error("SQL 语句数量与参数数量不匹配");
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
            CT::logger.error("无法准备批量 SQL 语句: {}", sqlite3_errmsg(db));
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
            CT::logger.error("无法执行批量 SQL 语句: {}", sqlite3_errmsg(db));
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
    if (mClosing) {
        std::promise<bool> p;
        p.set_value(false);
        return p.get_future();
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
    if (mClosing) {
        std::promise<std::vector<std::vector<std::string>>> p;
        p.set_value({});
        return p.get_future();
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

// 在线程池中等待 future 完成，然后在主线程执行回调
template <typename T, typename Callback>
void Sqlite3Wrapper::thenOnMainThread(std::future<T> fut, Callback&& callback) {
    if (!mThreadPool) {
        throw std::runtime_error("Thread pool not initialized. Call open() first.");
    }
    if (mClosing) {
        return;
    }

    mThreadPool->enqueue([fut = std::make_shared<std::future<T>>(std::move(fut)),
                          cb  = std::forward<Callback>(callback)]() mutable {
        try {
            T result = fut->get();
            ll::thread::ServerThreadExecutor::getDefault().execute([result = std::move(result), cb = std::move(cb)]() {
                cb(std::move(result));
            });
        } catch (const std::exception& e) {
            CT::logger.error("异步回调获取结果失败: {}", e.what());
        }
    });
}

// 在线程池中等待两个 future 完成，然后在主线程执行回调
template <typename T1, typename T2, typename Callback>
void Sqlite3Wrapper::thenOnMainThread(std::future<T1> fut1, std::future<T2> fut2, Callback&& callback) {
    if (!mThreadPool) {
        throw std::runtime_error("Thread pool not initialized. Call open() first.");
    }
    if (mClosing) {
        return;
    }

    mThreadPool->enqueue([fut1 = std::make_shared<std::future<T1>>(std::move(fut1)),
                          fut2 = std::make_shared<std::future<T2>>(std::move(fut2)),
                          cb   = std::forward<Callback>(callback)]() mutable {
        try {
            T1 result1 = fut1->get();
            T2 result2 = fut2->get();
            ll::thread::ServerThreadExecutor::getDefault().execute(
                [result1 = std::move(result1), result2 = std::move(result2), cb = std::move(cb)]() {
                    cb(std::move(result1), std::move(result2));
                }
            );
        } catch (const std::exception& e) {
            CT::logger.error("异步回调获取结果失败: {}", e.what());
        }
    });
}
