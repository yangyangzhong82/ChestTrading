#include "ChestRepository.h"
#include "DbRowParser.h"
#include "db/Sqlite3Wrapper.h"
#include "logger.h"

#include <chrono>

namespace CT {

ChestRepository& ChestRepository::getInstance() {
    static ChestRepository instance;
    return instance;
}

bool ChestRepository::insert(const ChestData& chest) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "INSERT OR REPLACE INTO chests (player_uuid, dim_id, pos_x, pos_y, pos_z, type, shop_name, "
        "enable_floating_text, enable_fake_item, is_public, last_restock_time) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CAST(strftime('%s', 'now') AS INTEGER));",
        chest.ownerUuid,
        chest.dimId,
        chest.pos.x,
        chest.pos.y,
        chest.pos.z,
        static_cast<int>(chest.type),
        chest.shopName,
        chest.enableFloatingText ? 1 : 0,
        chest.enableFakeItem ? 1 : 0,
        chest.isPublic ? 1 : 0
    );
}

bool ChestRepository::update(const ChestData& chest) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "UPDATE chests SET player_uuid = ?, type = ?, shop_name = ?, enable_floating_text = ?, "
        "enable_fake_item = ?, is_public = ? WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        chest.ownerUuid,
        static_cast<int>(chest.type),
        chest.shopName,
        chest.enableFloatingText ? 1 : 0,
        chest.enableFakeItem ? 1 : 0,
        chest.isPublic ? 1 : 0,
        chest.dimId,
        chest.pos.x,
        chest.pos.y,
        chest.pos.z
    );
}

bool ChestRepository::remove(BlockPos pos, int dimId) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "DELETE FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );
}

std::optional<ChestData> ChestRepository::findByPosition(BlockPos pos, int dimId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT player_uuid, type, shop_name, enable_floating_text, enable_fake_item, is_public "
         "FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    return parseSingleRow<ChestData>(results, 6, [&](DbRowParser r) {
        return ChestData{
            dimId,
            pos,
            r.getString(0),
            static_cast<ChestType>(r.getInt(1)),
            r.getString(2),
            r.getBool(3),
            r.getBool(4),
            r.getBool(5)
        };
    });
}

std::vector<ChestData> ChestRepository::findByOwner(const std::string& ownerUuid) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT dim_id, pos_x, pos_y, pos_z, type, shop_name, enable_floating_text, enable_fake_item, is_public "
         "FROM chests WHERE player_uuid = ?;",
        ownerUuid
    );

    return parseRows<ChestData>(results, 9, [&](DbRowParser r) {
        return ChestData{
            r.getInt(0),
            BlockPos{r.getInt(1), r.getInt(2), r.getInt(3)},
            ownerUuid,
            static_cast<ChestType>(r.getInt(4)),
            r.getString(5),
            r.getBool(6),
            r.getBool(7),
            r.getBool(8)
        };
    });
}

std::vector<ChestData> ChestRepository::findAll() {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query("SELECT dim_id, pos_x, pos_y, pos_z, player_uuid, type, shop_name, enable_floating_text, "
                             "enable_fake_item, is_public FROM chests ORDER BY player_uuid, dim_id;");

    return parseRows<ChestData>(results, 10, [](DbRowParser r) {
        return ChestData{
            r.getInt(0),
            BlockPos{r.getInt(1), r.getInt(2), r.getInt(3)},
            r.getString(4),
            static_cast<ChestType>(r.getInt(5)),
            r.getString(6),
            r.getBool(7),
            r.getBool(8),
            r.getBool(9)
        };
    });
}

std::vector<ChestData> ChestRepository::findAllPublicShops() {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT dim_id, pos_x, pos_y, pos_z, player_uuid, type, shop_name, enable_floating_text, "
        "enable_fake_item, is_public FROM chests "
        "WHERE is_public = 1 AND type IN (2, 3, 5, 6) ORDER BY player_uuid, dim_id;"
    );

    return parseRows<ChestData>(results, 10, [](DbRowParser r) {
        return ChestData{
            r.getInt(0),
            BlockPos{r.getInt(1), r.getInt(2), r.getInt(3)},
            r.getString(4),
            static_cast<ChestType>(r.getInt(5)),
            r.getString(6),
            r.getBool(7),
            r.getBool(8),
            r.getBool(9)
        };
    });
}

int ChestRepository::countByOwnerAndType(const std::string& ownerUuid, ChestType type) {
    auto& db = Sqlite3Wrapper::getInstance();
    auto  results =
        db.query("SELECT COUNT(*) FROM chests WHERE player_uuid = ? AND type = ?;", ownerUuid, static_cast<int>(type));

    if (!results.empty() && !results[0].empty()) {
        try {
            return std::stoi(results[0][0]);
        } catch (const std::exception& e) {
            logger.error(
                "统计箱子数量失败: 解析结果异常, uuid={}, type={}, error={}",
                ownerUuid,
                static_cast<int>(type),
                e.what()
            );
            return 0;
        }
    }
    return 0;
}

// === 过期检查 ===

std::vector<ChestData> ChestRepository::findExpiredChests(int64_t shopExpirySeconds) {
    auto& db = Sqlite3Wrapper::getInstance();
    auto  now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    std::vector<ChestData> expired;

    if (shopExpirySeconds <= 0) {
        return expired;
    }

    int64_t threshold = now - shopExpirySeconds;

    // 解析结果的通用函数
    auto parseRowsFn = [&](std::vector<std::vector<std::string>>& results) {
        for (const auto& row : results) {
            if (row.size() < 6) continue;
            try {
                ChestData data;
                data.dimId     = std::stoi(row[0]);
                data.pos       = BlockPos{std::stoi(row[1]), std::stoi(row[2]), std::stoi(row[3])};
                data.ownerUuid = row[4];
                data.type      = static_cast<ChestType>(std::stoi(row[5]));
                expired.push_back(std::move(data));
            } catch (const std::exception& e) {
                logger.error("findExpiredChests: 解析过期箱子行失败: {}", e.what());
            }
        }
    };

    // 商店（type=2）：超过阈值未补货，且箱子中没有任何 db_count > 0 的商品（空商店或全部售罄）
    auto shopResults = db.query(
        "SELECT c.dim_id, c.pos_x, c.pos_y, c.pos_z, c.player_uuid, c.type "
        "FROM chests c "
        "WHERE c.type = 2 AND c.last_restock_time > 0 AND c.last_restock_time <= ? "
        "AND NOT EXISTS ("
        "  SELECT 1 FROM shop_items si "
        "  WHERE si.dim_id = c.dim_id AND si.pos_x = c.pos_x "
        "  AND si.pos_y = c.pos_y AND si.pos_z = c.pos_z AND si.db_count > 0"
        ");",
        static_cast<long long>(threshold)
    );
    parseRowsFn(shopResults);

    // 回收商店（type=3）：超过阈值未补货，且没有任何有效委托
    // 有效委托 = 无限回收(max<=0) 或 未达到上限(current < max)
    auto recycleResults = db.query(
        "SELECT c.dim_id, c.pos_x, c.pos_y, c.pos_z, c.player_uuid, c.type "
        "FROM chests c "
        "WHERE c.type = 3 AND c.last_restock_time > 0 AND c.last_restock_time <= ? "
        "AND NOT EXISTS ("
        "  SELECT 1 FROM recycle_shop_items rsi "
        "  WHERE rsi.dim_id = c.dim_id AND rsi.pos_x = c.pos_x "
        "  AND rsi.pos_y = c.pos_y AND rsi.pos_z = c.pos_z "
        "  AND (rsi.max_recycle_count <= 0 OR rsi.current_recycled_count < rsi.max_recycle_count)"
        ");",
        static_cast<long long>(threshold)
    );
    parseRowsFn(recycleResults);

    return expired;
}

// === 补货时间管理 ===

bool ChestRepository::touchRestockTime(BlockPos pos, int dimId) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "UPDATE chests SET last_restock_time = CAST(strftime('%s', 'now') AS INTEGER) "
        "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );
}

// === 分享管理 ===

bool ChestRepository::addSharedPlayer(const SharedChestData& data) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "INSERT OR REPLACE INTO shared_chests (player_uuid, owner_uuid, dim_id, pos_x, pos_y, pos_z) "
        "VALUES (?, ?, ?, ?, ?, ?);",
        data.playerUuid,
        data.ownerUuid,
        data.dimId,
        data.pos.x,
        data.pos.y,
        data.pos.z
    );
}

bool ChestRepository::removeSharedPlayer(const std::string& playerUuid, BlockPos pos, int dimId) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "DELETE FROM shared_chests WHERE player_uuid = ? AND dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        playerUuid,
        dimId,
        pos.x,
        pos.y,
        pos.z
    );
}

std::vector<SharedChestData> ChestRepository::getSharedPlayers(BlockPos pos, int dimId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT player_uuid, owner_uuid FROM shared_chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    return parseRows<SharedChestData>(results, 2, [&](DbRowParser r) {
        return SharedChestData{r.getString(0), r.getString(1), dimId, pos};
    });
}

bool ChestRepository::isPlayerShared(const std::string& playerUuid, BlockPos pos, int dimId) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT 1 FROM shared_chests WHERE player_uuid = ? AND dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        playerUuid,
        dimId,
        pos.x,
        pos.y,
        pos.z
    );
    return !results.empty();
}

// === 配置更新 ===

bool ChestRepository::updateConfig(
    BlockPos pos,
    int      dimId,
    bool     enableFloatingText,
    bool     enableFakeItem,
    bool     isPublic
) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "UPDATE chests SET enable_floating_text = ?, enable_fake_item = ?, is_public = ? "
        "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        enableFloatingText ? 1 : 0,
        enableFakeItem ? 1 : 0,
        isPublic ? 1 : 0,
        dimId,
        pos.x,
        pos.y,
        pos.z
    );
}

bool ChestRepository::updateShopName(BlockPos pos, int dimId, const std::string& shopName) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "UPDATE chests SET shop_name = ? WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        shopName,
        dimId,
        pos.x,
        pos.y,
        pos.z
    );
}

int64_t ChestRepository::packChest(BlockPos pos, int dimId) {
    auto& db = Sqlite3Wrapper::getInstance();

    // 获取箱子信息
    auto chestInfo = findByPosition(pos, dimId);
    if (!chestInfo) return -1;

    Transaction txn(db);
    if (!txn.isActive()) {
        return -1;
    }

    int64_t packedTime =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    // 插入到 packed_chests
    if (!db.execute(
            "INSERT INTO packed_chests (player_uuid, orig_dim_id, orig_pos_x, orig_pos_y, orig_pos_z, "
            "type, shop_name, enable_floating_text, enable_fake_item, is_public, packed_time) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            chestInfo->ownerUuid,
            dimId,
            pos.x,
            pos.y,
            pos.z,
            static_cast<int>(chestInfo->type),
            chestInfo->shopName,
            chestInfo->enableFloatingText ? 1 : 0,
            chestInfo->enableFakeItem ? 1 : 0,
            chestInfo->isPublic ? 1 : 0,
            packedTime
        )) {
        return -1;
    }

    // 获取 packed_id
    int64_t packedId = db.getLastInsertRowId();
    if (packedId <= 0) return -1;

    // 复制商店商品
    if (!db.execute(
        "INSERT INTO packed_shop_items (packed_id, item_id, price, db_count, slot) "
        "SELECT ?, item_id, price, db_count, slot FROM shop_items "
        "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        packedId,
        dimId,
        pos.x,
        pos.y,
        pos.z
    )) {
        return -1;
    }

    // 复制回收商店商品
    if (!db.execute(
        "INSERT INTO packed_recycle_items (packed_id, item_id, price, min_durability, required_enchants, "
        "max_recycle_count, current_recycled_count, required_aux_value) "
        "SELECT ?, item_id, price, min_durability, required_enchants, max_recycle_count, "
        "current_recycled_count, required_aux_value FROM recycle_shop_items "
        "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        packedId,
        dimId,
        pos.x,
        pos.y,
        pos.z
    )) {
        return -1;
    }

    // 复制分享玩家
    if (!db.execute(
        "INSERT INTO packed_shared_chests (packed_id, player_uuid, owner_uuid) "
        "SELECT ?, player_uuid, owner_uuid FROM shared_chests "
        "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        packedId,
        dimId,
        pos.x,
        pos.y,
        pos.z
    )) {
        return -1;
    }

    if (!db.execute(
        "INSERT INTO packed_dynamic_pricing "
        "(packed_id, item_id, is_shop, price_tiers, stop_threshold, current_count, reset_interval_hours, "
        "last_reset_time, enabled) "
        "SELECT ?, item_id, is_shop, price_tiers, stop_threshold, current_count, reset_interval_hours, "
        "last_reset_time, enabled FROM dynamic_pricing "
        "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        packedId,
        dimId,
        pos.x,
        pos.y,
        pos.z
    )) {
        return -1;
    }

    if (!db.execute(
        "INSERT INTO packed_player_limits "
        "(packed_id, player_uuid, item_id, limit_count, limit_seconds, is_shop) "
        "SELECT ?, player_uuid, item_id, limit_count, limit_seconds, is_shop FROM player_limits "
        "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        packedId,
        dimId,
        pos.x,
        pos.y,
        pos.z
    )) {
        return -1;
    }

    // 删除原箱子数据（级联删除会清理关联表）
    if (!remove(pos, dimId)) {
        return -1;
    }

    if (!txn.commit()) {
        txn.rollback();
        return -1;
    }

    return packedId;
}

bool ChestRepository::unpackChest(int64_t packedId, BlockPos newPos, int newDimId) {
    auto& db = Sqlite3Wrapper::getInstance();

    // 获取打包的箱子信息
    auto result = db.query(
        "SELECT player_uuid, type, shop_name, enable_floating_text, enable_fake_item, is_public "
        "FROM packed_chests WHERE packed_id = ?;",
        packedId
    );
    if (result.empty()) return false;

    auto occupied = db.query(
        "SELECT 1 FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? LIMIT 1;",
        newDimId,
        newPos.x,
        newPos.y,
        newPos.z
    );
    if (!occupied.empty()) {
        return false;
    }

    Transaction txn(db);
    if (!txn.isActive()) {
        return false;
    }

    // 恢复箱子主表
    if (!db.execute(
            "INSERT INTO chests (player_uuid, dim_id, pos_x, pos_y, pos_z, type, shop_name, "
            "enable_floating_text, enable_fake_item, is_public, last_restock_time) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CAST(strftime('%s', 'now') AS INTEGER));",
            result[0][0], // player_uuid
            newDimId,
            newPos.x,
            newPos.y,
            newPos.z,
            std::stoi(result[0][1]), // type
            result[0][2],            // shop_name
            std::stoi(result[0][3]), // enable_floating_text
            std::stoi(result[0][4]), // enable_fake_item
            std::stoi(result[0][5])  // is_public
        )) {
        return false;
    }

    // 恢复商店商品
    if (!db.execute(
        "INSERT INTO shop_items (dim_id, pos_x, pos_y, pos_z, item_id, price, db_count, slot) "
        "SELECT ?, ?, ?, ?, item_id, price, db_count, slot FROM packed_shop_items WHERE packed_id = ?;",
        newDimId,
        newPos.x,
        newPos.y,
        newPos.z,
        packedId
    )) {
        return false;
    }

    if (!db.execute(
        "INSERT INTO dynamic_pricing "
        "(dim_id, pos_x, pos_y, pos_z, item_id, is_shop, price_tiers, stop_threshold, current_count, "
        "reset_interval_hours, last_reset_time, enabled) "
        "SELECT ?, ?, ?, ?, item_id, is_shop, price_tiers, stop_threshold, current_count, "
        "reset_interval_hours, last_reset_time, enabled FROM packed_dynamic_pricing WHERE packed_id = ?;",
        newDimId,
        newPos.x,
        newPos.y,
        newPos.z,
        packedId
    )) {
        return false;
    }

    if (!db.execute(
        "INSERT INTO player_limits "
        "(dim_id, pos_x, pos_y, pos_z, player_uuid, item_id, limit_count, limit_seconds, is_shop) "
        "SELECT ?, ?, ?, ?, player_uuid, item_id, limit_count, limit_seconds, is_shop "
        "FROM packed_player_limits WHERE packed_id = ?;",
        newDimId,
        newPos.x,
        newPos.y,
        newPos.z,
        packedId
    )) {
        return false;
    }

    // 恢复回收商店商品
    if (!db.execute(
        "INSERT INTO recycle_shop_items (dim_id, pos_x, pos_y, pos_z, item_id, price, min_durability, "
        "required_enchants, max_recycle_count, current_recycled_count, required_aux_value) "
        "SELECT ?, ?, ?, ?, item_id, price, min_durability, required_enchants, max_recycle_count, "
        "current_recycled_count, required_aux_value FROM packed_recycle_items WHERE packed_id = ?;",
        newDimId,
        newPos.x,
        newPos.y,
        newPos.z,
        packedId
    )) {
        return false;
    }

    // 恢复分享玩家
    if (!db.execute(
        "INSERT INTO shared_chests (player_uuid, owner_uuid, dim_id, pos_x, pos_y, pos_z) "
        "SELECT player_uuid, owner_uuid, ?, ?, ?, ? FROM packed_shared_chests WHERE packed_id = ?;",
        newDimId,
        newPos.x,
        newPos.y,
        newPos.z,
        packedId
    )) {
        return false;
    }

    // 删除打包数据
    if (!db.execute("DELETE FROM packed_chests WHERE packed_id = ?;", packedId)) {
        return false;
    }

    if (!txn.commit()) {
        txn.rollback();
        return false;
    }

    return true;
}

} // namespace CT
