#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>
#include <variant>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>

class Sqlite3Wrapper {
public:
    // 获取单例实例
    static Sqlite3Wrapper& getInstance() {
        static Sqlite3Wrapper instance;
        return instance;
    }

    // 删除拷贝构造函数和赋值运算符
    Sqlite3Wrapper(const Sqlite3Wrapper&) = delete;
    Sqlite3Wrapper& operator=(const Sqlite3Wrapper&) = delete;

    // 定义可以绑定到SQL语句的参数类型
    using Value = std::variant<int, long long, double, std::string, const char*>;

    bool open(const std::string& db_path);
    void close();

    // 不安全的旧方法,建议不再使用
    bool execute_unsafe(const std::string& sql);
    std::vector<std::vector<std::string>> query_unsafe(const std::string& sql);

    // 支持参数绑定的安全 execute 方法
    template<typename... Args>
    bool execute(const std::string& sql, Args&&... args);

    // 支持参数绑定的安全 query 方法
    template<typename... Args>
    std::vector<std::vector<std::string>> query(const std::string& sql, Args&&... args);

    // 事务管理
    bool beginTransaction();
    bool commit();
    bool rollback();

    // 批量操作(自动使用事务)
    template<typename... Args>
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

    // 获取数据库统计信息
    struct DbStats {
        int cacheHits = 0;
        int cacheMisses = 0;
        int queryCount = 0;
        int transactionCount = 0;
    };
    DbStats getStats() const { return mStats; }

private:
    Sqlite3Wrapper();
    ~Sqlite3Wrapper();

    sqlite3* db;
    std::recursive_mutex mDbMutex; // 数据库操作互斥锁
    bool mInTransaction = false;

    // 查询缓存相关
    struct CacheEntry {
        std::vector<std::vector<std::string>> result;
        std::chrono::steady_clock::time_point timestamp;
    };
    std::unordered_map<std::string, CacheEntry> mQueryCache;
    std::mutex mCacheMutex;
    int mCacheTimeoutSeconds = 60;  // 默认缓存60秒
    bool mCacheEnabled = true;

    // 统计信息
    mutable DbStats mStats;

    // 内部辅助函数,用于绑定参数
    template<size_t I = 1, typename T, typename... Args>
    void bind_args(sqlite3_stmt* stmt, T&& value, Args&&... args);

    template<size_t I = 1>
    void bind_args(sqlite3_stmt* stmt) {} // 递归终止

    // 生成缓存键
    std::string generateCacheKey(const std::string& sql, const std::vector<Value>& params);

    // 从缓存获取结果
    bool getCachedResult(const std::string& key, std::vector<std::vector<std::string>>& result);

    // 存储结果到缓存
    void setCachedResult(const std::string& key, const std::vector<std::vector<std::string>>& result);

    // 执行准备好的语句
    template<typename... Args>
    bool executeStatement(sqlite3_stmt* stmt, Args&&... args);

    // 查询准备好的语句
    template<typename... Args>
    std::vector<std::vector<std::string>> queryStatement(sqlite3_stmt* stmt, Args&&... args);
};


// --- 模板函数的实现需要放在头文件中 ---

template<typename... Args>
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

template<typename... Args>
std::vector<std::vector<std::string>> Sqlite3Wrapper::query(const std::string& sql, Args&&... args) {
    std::vector<std::vector<std::string>> results;
    
    // 生成缓存键
    std::vector<Value> params;
    if constexpr (sizeof...(args) > 0) {
        params = {Value(std::forward<Args>(args))...};
    }
    std::string cacheKey = generateCacheKey(sql, params);

    // 尝试从缓存获取
    if (mCacheEnabled && getCachedResult(cacheKey, results)) {
        mStats.cacheHits++;
        return results;
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
        int col_count = sqlite3_column_count(stmt);
        for (int i = 0; i < col_count; ++i) {
            const char* col_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
            row.push_back(col_text ? col_text : "NULL");
        }
        results.push_back(row);
    }

    sqlite3_finalize(stmt);

    // 存储到缓存
    if (mCacheEnabled) {
        setCachedResult(cacheKey, results);
    }

    return results;
}

template<size_t I, typename T, typename... Args>
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

template<typename... Args>
bool Sqlite3Wrapper::executeBatch(const std::vector<std::string>& sqlStatements, const std::vector<std::vector<Value>>& paramsList) {
    if (sqlStatements.size() != paramsList.size()) {
        std::cerr << "SQL statements count doesn't match parameters count" << std::endl;
        return false;
    }

    // 自动开启事务
    if (!beginTransaction()) {
        return false;
    }

    bool allSuccess = true;
    
    for (size_t i = 0; i < sqlStatements.size(); ++i) {
        const auto& sql = sqlStatements[i];
        const auto& params = paramsList[i];

        std::lock_guard<std::recursive_mutex> lock(mDbMutex);
        
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            std::cerr << "Failed to prepare batch statement: " << sqlite3_errmsg(db) << std::endl;
            allSuccess = false;
            break;
        }

        // 绑定参数
        for (size_t j = 0; j < params.size(); ++j) {
            std::visit([&](auto&& arg) {
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
            }, params[j]);
        }

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Failed to execute batch statement: " << sqlite3_errmsg(db) << std::endl;
            allSuccess = false;
            sqlite3_finalize(stmt);
            break;
        }

        sqlite3_finalize(stmt);
    }

    // 根据执行结果提交或回滚事务
    if (allSuccess) {
        return commit();
    } else {
        rollback();
        return false;
    }
}
