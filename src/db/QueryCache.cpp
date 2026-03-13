#include "QueryCache.h"
#include <algorithm>

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

bool QueryCache::shouldSkip(const std::string& sql) {
    std::string lowerSql = sql;
    std::transform(lowerSql.begin(), lowerSql.end(), lowerSql.begin(), [](unsigned char c) { return std::tolower(c); });

    auto firstNonSpace = lowerSql.find_first_not_of(" \t\r\n");
    if (firstNonSpace == std::string::npos) {
        return true;
    }
    lowerSql.erase(0, firstNonSpace);

    const bool cacheableRead = lowerSql.rfind("select", 0) == 0 || lowerSql.rfind("with", 0) == 0;
    if (!cacheableRead) {
        return true;
    }

    if (lowerSql.rfind("pragma", 0) == 0 || lowerSql.find("last_insert_rowid") != std::string::npos) {
        return true;
    }

    return lowerSql.find("chests") != std::string::npos || lowerSql.find("shared_chests") != std::string::npos
        || lowerSql.find("recycle_shop_items") != std::string::npos || lowerSql.find("shop_items") != std::string::npos;
}
