#pragma once

#include <string>

namespace CT::TimeUtils {

// SQLite CURRENT_TIMESTAMP 返回 UTC 时间，统一在展示前转为本地时间。
std::string utcSqliteTimestampToLocal(const std::string& timestamp);

} // namespace CT::TimeUtils
