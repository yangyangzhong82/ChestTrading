#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

class QueryCache {
public:
    using Value = std::variant<int, long long, double, std::string, const char*>;

    void clear();
    void clearForTable(const std::string& tableName);
    void setTimeout(int seconds) { mTimeoutSeconds = seconds; }
    void setEnabled(bool enable);

    bool get(size_t key, const std::string& sql, std::vector<std::vector<std::string>>& result);
    void set(size_t key, const std::string& sql, const std::vector<std::vector<std::string>>& result);

    // 生成缓存键
    static size_t generateKey(const std::string& sql, const std::vector<Value>& params);

    template <typename... Args>
    static size_t generateKeyFast(const std::string& sql, Args&&... args);

    // 判断查询是否应跳过缓存
    static bool shouldSkip(const std::string& sql);

private:
    struct CacheEntry {
        std::string                           sql;
        std::vector<std::vector<std::string>> result;
        std::chrono::steady_clock::time_point timestamp;
    };

    std::unordered_map<size_t, CacheEntry> mCache;
    std::mutex                             mMutex;
    int                                    mTimeoutSeconds = 60;
    bool                                   mEnabled        = true;
};

// 哈希辅助
namespace detail {
inline void   hashCombine(size_t& seed, size_t h) { seed ^= h + 0x9e3779b9 + (seed << 6) + (seed >> 2); }
inline size_t hashValue(int v) { return std::hash<int>{}(v); }
inline size_t hashValue(long long v) { return std::hash<long long>{}(v); }
inline size_t hashValue(double v) { return std::hash<double>{}(v); }
inline size_t hashValue(const std::string& v) { return std::hash<std::string>{}(v); }
inline size_t hashValue(const char* v) { return std::hash<std::string_view>{}(v); }
} // namespace detail

template <typename... Args>
size_t QueryCache::generateKeyFast(const std::string& sql, Args&&... args) {
    size_t hash = std::hash<std::string>{}(sql);
    if constexpr (sizeof...(args) > 0) {
        (detail::hashCombine(hash, detail::hashValue(args)), ...);
    }
    return hash;
}
