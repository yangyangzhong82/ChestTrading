#include "ChestRepository.h"
#include "db/Sqlite3Wrapper.h"
#include "logger.h"
#include <stdexcept>

namespace CT {

ChestRepository& ChestRepository::getInstance() {
    static ChestRepository instance;
    return instance;
}

bool ChestRepository::insert(const ChestData& chest) {
    auto& db = Sqlite3Wrapper::getInstance();
    return db.execute(
        "INSERT OR REPLACE INTO chests (player_uuid, dim_id, pos_x, pos_y, pos_z, type, shop_name, "
        "enable_floating_text, enable_fake_item, is_public) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
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

    if (results.empty() || results[0].size() < 6) {
        return std::nullopt;
    }

    try {
        const auto& row = results[0];
        ChestData   data;
        data.dimId              = dimId;
        data.pos                = pos;
        data.ownerUuid          = row[0];
        data.type               = static_cast<ChestType>(std::stoi(row[1]));
        data.shopName           = row[2];
        data.enableFloatingText = (std::stoi(row[3]) != 0);
        data.enableFakeItem     = (std::stoi(row[4]) != 0);
        data.isPublic           = (std::stoi(row[5]) != 0);
        return data;
    } catch (const std::exception& e) {
        logger.error("Failed to parse chest data: {}", e.what());
        return std::nullopt;
    }
}

std::vector<ChestData> ChestRepository::findByOwner(const std::string& ownerUuid) {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT dim_id, pos_x, pos_y, pos_z, type, shop_name, enable_floating_text, enable_fake_item, is_public "
         "FROM chests WHERE player_uuid = ?;",
        ownerUuid
    );

    std::vector<ChestData> chests;
    for (const auto& row : results) {
        if (row.size() >= 9) {
            try {
                ChestData data;
                data.ownerUuid          = ownerUuid;
                data.dimId              = std::stoi(row[0]);
                data.pos                = BlockPos{std::stoi(row[1]), std::stoi(row[2]), std::stoi(row[3])};
                data.type               = static_cast<ChestType>(std::stoi(row[4]));
                data.shopName           = row[5];
                data.enableFloatingText = (std::stoi(row[6]) != 0);
                data.enableFakeItem     = (std::stoi(row[7]) != 0);
                data.isPublic           = (std::stoi(row[8]) != 0);
                chests.push_back(data);
            } catch (const std::exception& e) {
                logger.error("Failed to parse chest data for owner {}: {}", ownerUuid, e.what());
            }
        }
    }
    return chests;
}

std::vector<ChestData> ChestRepository::findAll() {
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query("SELECT dim_id, pos_x, pos_y, pos_z, player_uuid, type, shop_name, enable_floating_text, "
                             "enable_fake_item, is_public FROM chests ORDER BY player_uuid, dim_id;");

    std::vector<ChestData> chests;
    for (const auto& row : results) {
        if (row.size() >= 10) {
            try {
                ChestData data;
                data.dimId              = std::stoi(row[0]);
                data.pos                = BlockPos{std::stoi(row[1]), std::stoi(row[2]), std::stoi(row[3])};
                data.ownerUuid          = row[4];
                data.type               = static_cast<ChestType>(std::stoi(row[5]));
                data.shopName           = row[6];
                data.enableFloatingText = (std::stoi(row[7]) != 0);
                data.enableFakeItem     = (std::stoi(row[8]) != 0);
                data.isPublic           = (std::stoi(row[9]) != 0);
                chests.push_back(data);
            } catch (const std::exception& e) {
                logger.error("Failed to parse chest data: {}", e.what());
            }
        }
    }
    return chests;
}

int ChestRepository::countByOwnerAndType(const std::string& ownerUuid, ChestType type) {
    auto& db = Sqlite3Wrapper::getInstance();
    auto  results =
        db.query("SELECT COUNT(*) FROM chests WHERE player_uuid = ? AND type = ?;", ownerUuid, static_cast<int>(type));

    if (!results.empty() && !results[0].empty()) {
        try {
            return std::stoi(results[0][0]);
        } catch (...) {
            return 0;
        }
    }
    return 0;
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

    std::vector<SharedChestData> shared;
    for (const auto& row : results) {
        if (row.size() >= 2) {
            SharedChestData data;
            data.playerUuid = row[0];
            data.ownerUuid  = row[1];
            data.dimId      = dimId;
            data.pos        = pos;
            shared.push_back(data);
        }
    }
    return shared;
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

} // namespace CT