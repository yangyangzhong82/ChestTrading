#include "TimeUtils.h"

#include <cstdio>
#include <ctime>

namespace CT::TimeUtils {

std::string utcSqliteTimestampToLocal(const std::string& timestamp) {
    if (timestamp.size() != 19) {
        return timestamp;
    }

    int year   = 0;
    int month  = 0;
    int day    = 0;
    int hour   = 0;
    int minute = 0;
    int second = 0;
    if (sscanf_s(
            timestamp.c_str(),
            "%d-%d-%d %d:%d:%d",
            &year,
            &month,
            &day,
            &hour,
            &minute,
            &second
        )
        != 6) {
        return timestamp;
    }

    std::tm utcTm = {};
    utcTm.tm_year = year - 1900;
    utcTm.tm_mon  = month - 1;
    utcTm.tm_mday = day;
    utcTm.tm_hour = hour;
    utcTm.tm_min  = minute;
    utcTm.tm_sec  = second;
    utcTm.tm_isdst = -1;

    std::time_t utcTime = _mkgmtime(&utcTm);
    if (utcTime == static_cast<std::time_t>(-1)) {
        return timestamp;
    }

    std::tm localTm = {};
    if (localtime_s(&localTm, &utcTime) != 0) {
        return timestamp;
    }

    char buffer[20] = {};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTm) == 0) {
        return timestamp;
    }

    return buffer;
}

} // namespace CT::TimeUtils
