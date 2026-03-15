#include "QueryCache.h"
#include <algorithm>
#include <cctype>
#include <cstring>

void QueryCache::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    mCache.clear();
}

void QueryCache::clearForTable(const std::string& tableName) {
    if (tableName.empty()) {
        clear();
        return;
    }
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto it = mCache.begin(); it != mCache.end();) {
        if (it->second.sql.find(tableName) != std::string::npos) {
            it = mCache.erase(it);
        } else {
            ++it;
        }
    }
}

void QueryCache::setEnabled(bool enable) {
    mEnabled = enable;
    if (!enable) {
        clear();
    }
}

bool QueryCache::get(size_t key, const std::string& sql, std::vector<std::vector<std::string>>& result) {
    if (!mEnabled) return false;

    std::lock_guard<std::mutex> lock(mMutex);
    auto                        it = mCache.find(key);
    if (it == mCache.end() || it->second.sql != sql) {
        return false;
    }

    auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - it->second.timestamp)
            .count();

    if (elapsed > mTimeoutSeconds) {
        mCache.erase(it);
        return false;
    }

    result = it->second.result;
    return true;
}

void QueryCache::set(size_t key, const std::string& sql, const std::vector<std::vector<std::string>>& result) {
    if (!mEnabled) return;

    std::lock_guard<std::mutex> lock(mMutex);
    mCache[key] = {sql, result, std::chrono::steady_clock::now()};
}

size_t QueryCache::generateKey(const std::string& sql, const std::vector<Value>& params) {
    size_t hash = std::hash<std::string>{}(sql);
    for (const auto& param : params) {
        size_t h = std::visit(
            [](auto&& arg) -> size_t {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, int>) {
                    return std::hash<int>{}(arg);
                } else if constexpr (std::is_same_v<T, long long>) {
                    return std::hash<long long>{}(arg);
                } else if constexpr (std::is_same_v<T, double>) {
                    return std::hash<double>{}(arg);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return std::hash<std::string>{}(arg);
                } else if constexpr (std::is_same_v<T, const char*>) {
                    return std::hash<std::string_view>{}(arg);
                }
                return 0;
            },
            param
        );
        hash ^= h + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    return hash;
}

namespace {

// 大小写不敏感前缀匹配（避免全量 toLower）
bool startsWithCI(const char* str, const char* prefix) {
    while (*prefix) {
        if (std::tolower(static_cast<unsigned char>(*str)) != static_cast<unsigned char>(*prefix)) return false;
        ++str;
        ++prefix;
    }
    return true;
}

// 大小写不敏感子串搜索
bool containsCI(const std::string& haystack, const char* needle) {
    size_t needleLen = std::strlen(needle);
    if (needleLen > haystack.size()) return false;
    for (size_t i = 0; i <= haystack.size() - needleLen; ++i) {
        if (startsWithCI(haystack.data() + i, needle)) return true;
    }
    return false;
}

} // namespace

bool QueryCache::shouldSkip(const std::string& sql) {
    // 跳过前导空白
    size_t firstNonSpace = 0;
    while (firstNonSpace < sql.size()
           && (sql[firstNonSpace] == ' ' || sql[firstNonSpace] == '\t' || sql[firstNonSpace] == '\r'
               || sql[firstNonSpace] == '\n')) {
        ++firstNonSpace;
    }
    if (firstNonSpace >= sql.size()) return true;

    const char* start = sql.data() + firstNonSpace;

    // 仅缓存 SELECT / WITH 开头的查询
    bool cacheableRead = startsWithCI(start, "select") || startsWithCI(start, "with");
    if (!cacheableRead) return true;

    // 排除 PRAGMA 和 last_insert_rowid
    if (startsWithCI(start, "pragma") || containsCI(sql, "last_insert_rowid")) return true;

    // 排除频繁变动的表
    return containsCI(sql, "chests") || containsCI(sql, "shared_chests") || containsCI(sql, "recycle_shop_items")
        || containsCI(sql, "shop_items");
}
