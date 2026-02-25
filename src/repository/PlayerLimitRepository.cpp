#include "PlayerLimitRepository.h"
#include "DbRowParser.h"
#include "db/Sqlite3Wrapper.h"
#include "logger.h"

namespace CT {

PlayerLimitRepository& PlayerLimitRepository::getInstance() {
    static PlayerLimitRepository instance;
    return instance;
}

bool PlayerLimitRepository::upsertLimit(const PlayerLimitConfig& config) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "INSERT INTO player_limits "
        "(dim_id, pos_x, pos_y, pos_z, player_uuid, item_id, limit_count, limit_seconds, is_shop) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(dim_id, pos_x, pos_y, pos_z, player_uuid, is_shop, item_id) DO UPDATE SET "
        "limit_count = excluded.limit_count, limit_seconds = excluded.limit_seconds;",
        config.dimId,
        config.pos.x,
        config.pos.y,
        config.pos.z,
        config.playerUuid,
        config.itemId,
        config.limitCount,
        config.limitSeconds,
        config.isShop ? 1 : 0
    );
}

bool
PlayerLimitRepository::removeLimit(BlockPos pos, int dimId, const std::string& playerUuid, bool isShop, int itemId) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "DELETE FROM player_limits WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? "
        "AND player_uuid = ? AND is_shop = ? AND item_id = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        playerUuid,
        isShop ? 1 : 0,
        itemId
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
PlayerLimitRepository::getLimit(BlockPos pos, int dimId, const std::string& playerUuid, bool isShop, int itemId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT limit_count, limit_seconds FROM player_limits "
         "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? "
         "AND player_uuid = ? AND is_shop = ? AND item_id = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        playerUuid,
        isShop ? 1 : 0,
        itemId
    );

    return parseSingleRow<PlayerLimitConfig>(results, 2, [&](DbRowParser r) {
        return PlayerLimitConfig{dimId, pos, playerUuid, itemId, r.getInt(0), r.getInt(1), isShop};
    });
}

std::vector<PlayerLimitConfig> PlayerLimitRepository::getAllLimits(BlockPos pos, int dimId, bool isShop, int itemId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    std::vector<std::vector<std::string>> results;
    if (itemId >= 0) {
        results = db.query(
            "SELECT player_uuid, item_id, limit_count, limit_seconds FROM player_limits "
            "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND is_shop = ? AND item_id = ?;",
            dimId,
            pos.x,
            pos.y,
            pos.z,
            isShop ? 1 : 0,
            itemId
        );
    } else {
        results = db.query(
            "SELECT player_uuid, item_id, limit_count, limit_seconds FROM player_limits "
            "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND is_shop = ?;",
            dimId,
            pos.x,
            pos.y,
            pos.z,
            isShop ? 1 : 0
        );
    }

    return parseRows<PlayerLimitConfig>(results, 4, [&](DbRowParser r) {
        return PlayerLimitConfig{dimId, pos, r.getString(0), r.getInt(1), r.getInt(2), r.getInt(3), isShop};
    });
}

int PlayerLimitRepository::getTradeCountInWindow(
    BlockPos           pos,
    int                dimId,
    const std::string& playerUuid,
    int                windowSeconds,
    bool               isShop,
    int                itemId
) {
    auto& db = Sqlite3Wrapper::getInstance();

    std::string table    = isShop ? "purchase_records" : "recycle_records";
    std::string col      = isShop ? "buyer_uuid" : "recycler_uuid";
    std::string countCol = isShop ? "purchase_count" : "recycle_count";

    std::vector<std::vector<std::string>> results;
    if (itemId >= 0) {
        results = db.query(
            "SELECT COALESCE(SUM(" + countCol + "), 0) FROM " + table
                + " WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND " + col
                + " = ? AND item_id = ? AND timestamp >= datetime('now', '-' || ? || ' seconds') "
                  "AND timestamp >= COALESCE(("
                  "SELECT datetime(MAX(last_reset_time), 'unixepoch') FROM player_limit_resets "
                  "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND is_shop = ? "
                  "AND (item_id = ? OR item_id = 0)"
                  "), '1970-01-01 00:00:00');",
            dimId,
            pos.x,
            pos.y,
            pos.z,
            playerUuid,
            itemId,
            windowSeconds,
            dimId,
            pos.x,
            pos.y,
            pos.z,
            isShop ? 1 : 0,
            itemId
        );
    } else {
        results = db.query(
            "SELECT COALESCE(SUM(" + countCol + "), 0) FROM " + table
                + " WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND " + col
                + " = ? AND timestamp >= datetime('now', '-' || ? || ' seconds') "
                  "AND timestamp >= COALESCE(("
                  "SELECT datetime(MAX(last_reset_time), 'unixepoch') FROM player_limit_resets "
                  "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND is_shop = ? AND item_id = 0"
                  "), '1970-01-01 00:00:00');",
            dimId,
            pos.x,
            pos.y,
            pos.z,
            playerUuid,
            windowSeconds,
            dimId,
            pos.x,
            pos.y,
            pos.z,
            isShop ? 1 : 0
        );
    }

    if (!results.empty() && !results[0].empty()) {
        try {
            return std::stoi(results[0][0]);
        } catch (const std::exception& e) {
            logger.error(
                "查询交易记录数量失败: 解析结果异常, playerUuid={}, itemId={}, error={}",
                playerUuid,
                itemId,
                e.what()
            );
            return 0;
        }
    }
    return 0;
}

bool PlayerLimitRepository::upsertLimitResetPoint(
    BlockPos pos,
    int      dimId,
    bool     isShop,
    int64_t  resetTime,
    int      itemId
) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "INSERT INTO player_limit_resets (dim_id, pos_x, pos_y, pos_z, is_shop, item_id, last_reset_time) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(dim_id, pos_x, pos_y, pos_z, is_shop, item_id) DO UPDATE SET "
        "last_reset_time = excluded.last_reset_time;",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        isShop ? 1 : 0,
        itemId,
        resetTime
    );
}

} // namespace CT
