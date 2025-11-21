#pragma once

#include <string>
#include <vector>
#include <sqlite3.h>
#include <variant>
#include <iostream>

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

    // 不安全的旧方法，建议不再使用
    bool execute_unsafe(const std::string& sql);
    std::vector<std::vector<std::string>> query_unsafe(const std::string& sql);

    // 支持参数绑定的安全 execute 方法
    template<typename... Args>
    bool execute(const std::string& sql, Args&&... args);

    // 支持参数绑定的安全 query 方法
    template<typename... Args>
    std::vector<std::vector<std::string>> query(const std::string& sql, Args&&... args);

    // 检查列是否存在
    bool isColumnExists(const std::string& tableName, const std::string& columnName);

private:
    Sqlite3Wrapper();
    ~Sqlite3Wrapper();

    sqlite3* db;

    // 内部辅助函数，用于绑定参数
    template<size_t I = 1, typename T, typename... Args>
    void bind_args(sqlite3_stmt* stmt, T&& value, Args&&... args);

    template<size_t I = 1>
    void bind_args(sqlite3_stmt* stmt) {} // 递归终止
};


// --- 模板函数的实现需要放在头文件中 ---

template<typename... Args>
bool Sqlite3Wrapper::execute(const std::string& sql, Args&&... args) {
    if (!db) return false;

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
    return result;
}

template<typename... Args>
std::vector<std::vector<std::string>> Sqlite3Wrapper::query(const std::string& sql, Args&&... args) {
    std::vector<std::vector<std::string>> results;
    if (!db) return results;

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
