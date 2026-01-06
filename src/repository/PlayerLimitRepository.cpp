#include "PlayerLimitRepository.h"
#include "DbRowParser.h"
#include "db/Sqlite3Wrapper.h"

namespace CT {

PlayerLimitRepository& PlayerLimitRepository::getInstance() {
    static PlayerLimitRepository instance;
    return instance;
}

bool PlayerLimitRepository::upsertLimit(const PlayerLimitConfig& config) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "INSERT INTO player_limits (dim_id, pos_x, pos_y, pos_z, player_uuid, limit_count, limit_seconds, is_shop) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(dim_id, pos_x, pos_y, pos_z, player_uuid, is_shop) DO UPDATE SET "
        "limit_count = excluded.limit_count, limit_seconds = excluded.limit_seconds;",
        config.dimId,
        config.pos.x,
        config.pos.y,
        config.pos.z,
        config.playerUuid,
        config.limitCount,
        config.limitSeconds,
        config.isShop ? 1 : 0
    );
}

bool PlayerLimitRepository::removeLimit(BlockPos pos, int dimId, const std::string& playerUuid, bool isShop) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "DELETE FROM player_limits WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? "
        "AND player_uuid = ? AND is_shop = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        playerUuid,
        isShop ? 1 : 0
    );
}

bool PlayerLimitRepository::removeAllLimits(BlockPos pos, int dimId) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "DELETE FROM player_limits WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );
}

std::optional<PlayerLimitConfig>
PlayerLimitRepository::getLimit(BlockPos pos, int dimId, const std::string& playerUuid, bool isShop) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT limit_count, limit_seconds FROM player_limits "
         "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND player_uuid = ? AND is_shop = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        playerUuid,
        isShop ? 1 : 0
    );

    return parseSingleRow<PlayerLimitConfig>(results, 2, [&](DbRowParser r) {
        return PlayerLimitConfig{dimId, pos, playerUuid, r.getInt(0), r.getInt(1), isShop};
    });
}

std::vector<PlayerLimitConfig> PlayerLimitRepository::getAllLimits(BlockPos pos, int dimId, bool isShop) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT player_uuid, limit_count, limit_seconds FROM player_limits "
         "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND is_shop = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        isShop ? 1 : 0
    );

    return parseRows<PlayerLimitConfig>(results, 3, [&](DbRowParser r) {
        return PlayerLimitConfig{dimId, pos, r.getString(0), r.getInt(1), r.getInt(2), isShop};
    });
}

int PlayerLimitRepository::getTradeCountInWindow(
    BlockPos           pos,
    int                dimId,
    const std::string& playerUuid,
    int                windowSeconds,
    bool               isShop
) {
    auto& db = Sqlite3Wrapper::getInstance();

    std::string table    = isShop ? "purchase_records" : "recycle_records";
    std::string col      = isShop ? "buyer_uuid" : "recycler_uuid";
    std::string countCol = isShop ? "purchase_count" : "recycle_count";

    auto results = db.query(
        "SELECT COALESCE(SUM(" + countCol + "), 0) FROM " + table
            + " WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND " + col
            + " = ? AND timestamp >= datetime('now', '-' || ? || ' seconds');",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        playerUuid,
        windowSeconds
    );

    if (!results.empty() && !results[0].empty()) {
        try {
            return std::stoi(results[0][0]);
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

} // namespace CT
