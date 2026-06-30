#include "LandSettingRepository.h"
#include "db/Sqlite3Wrapper.h"

namespace CT {

LandSettingRepository& LandSettingRepository::getInstance() {
    static LandSettingRepository instance;
    return instance;
}

std::optional<int> LandSettingRepository::getAllowedTypesMask(int64_t landId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT allowed_mask FROM land_chest_settings WHERE land_id = ?;",
        static_cast<long long>(landId)
    );

    if (results.empty() || results[0].empty()) {
        return std::nullopt;
    }

    try {
        return std::stoi(results[0][0]);
    } catch (...) {
        return std::nullopt;
    }
}

bool LandSettingRepository::setAllowedTypesMask(int64_t landId, int mask) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "INSERT OR REPLACE INTO land_chest_settings (land_id, allowed_mask) VALUES (?, ?);",
        static_cast<long long>(landId),
        mask
    );
}

bool LandSettingRepository::remove(int64_t landId) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute("DELETE FROM land_chest_settings WHERE land_id = ?;", static_cast<long long>(landId));
}

} // namespace CT
