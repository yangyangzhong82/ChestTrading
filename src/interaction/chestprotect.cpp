#include "interaction/chestprotect.h"
#include "ll/api/form/SimpleForm.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "db/Sqlite3Wrapper.h" // 引入 Sqlite3Wrapper
#include "mc/world/level/BlockSource.h" // 引入 BlockSource
#include "mc/world/level/block/actor/ChestBlockActor.h" // 引入 ChestBlockActor

namespace CT {



std::tuple<bool, std::string, ChestType> getChestDetails(BlockPos pos, int dimId) {
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    std::vector<std::vector<std::string>> results = db.query(
        "SELECT player_uuid, type FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    if (!results.empty() && results[0].size() >= 2) {
        std::string playerUuid = results[0][0];
        ChestType   chestType  = static_cast<ChestType>(std::stoi(results[0][1]));
        return {true, playerUuid, chestType}; // 箱子已设置
    }
    return {false, "", ChestType::Locked}; // 箱子未设置，返回默认值
}

bool setChest(const std::string& player_uuid, BlockPos pos, int dimId, BlockSource& region, ChestType type) {
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    bool success = db.execute(
        "INSERT OR REPLACE INTO chests (player_uuid, dim_id, pos_x, pos_y, pos_z, type) VALUES (?, ?, ?, ?, ?, ?);",
        player_uuid,
        dimId,
        pos.x,
        pos.y,
        pos.z,
        static_cast<int>(type)
    );

    if (success) {
        auto* blockActor = region.getBlockEntity(pos);
        if (blockActor) {
            auto chest = static_cast<class ChestBlockActor*>(blockActor);
            if (chest->mLargeChestPaired) {
                BlockPos pairedChestPos = chest->mLargeChestPairedPosition;
                db.execute(
                    "INSERT OR REPLACE INTO chests (player_uuid, dim_id, pos_x, pos_y, pos_z, type) VALUES (?, ?, ?, ?, ?, ?);",
                    player_uuid,
                    dimId,
                    pairedChestPos.x,
                    pairedChestPos.y,
                    pairedChestPos.z,
                    static_cast<int>(type)
                );
            }
        }
    }
    return success;
}

bool removeChest(BlockPos pos, int dimId, BlockSource& region) {
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    bool success = db.execute(
        "DELETE FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    if (success) {
        auto* blockActor = region.getBlockEntity(pos);
        if (blockActor) {
            auto chest = static_cast<class ChestBlockActor*>(blockActor);
            if (chest->mLargeChestPaired) {
                BlockPos pairedChestPos = chest->mLargeChestPairedPosition;
                db.execute(
                    "DELETE FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
                    dimId,
                    pairedChestPos.x,
                    pairedChestPos.y,
                    pairedChestPos.z
                );
            }
        }
    }
    return success;
}

bool addSharedPlayer(const std::string& owner_uuid, const std::string& shared_player_uuid, BlockPos pos, int dimId) {
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    bool success = db.execute(
        "INSERT OR REPLACE INTO shared_chests (player_uuid, dim_id, pos_x, pos_y, pos_z) VALUES (?, ?, ?, ?, ?);",
        shared_player_uuid,
        static_cast<int>(dimId),
        pos.x,
        pos.y,
        pos.z
    );

    if (success) {
        // 检查是否是双箱子，如果是，也为配对的箱子添加分享玩家
        // 注意：这里需要获取 BlockSource，但当前函数签名中没有。
        // 考虑到这个函数可能在没有 BlockSource 的情况下被调用，
        // 暂时不处理双箱子，或者在调用处传入 BlockSource。
        // 为了简化，这里假设只处理单个箱子。
        // 如果需要处理双箱子，需要修改函数签名或在调用处获取 BlockSource。
    }
    return success;
}

bool removeSharedPlayer(const std::string& shared_player_uuid, BlockPos pos, int dimId) {
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    bool success = db.execute(
        "DELETE FROM shared_chests WHERE player_uuid = ? AND dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        shared_player_uuid,
        static_cast<int>(dimId),
        pos.x,
        pos.y,
        pos.z
    );

    if (success) {
        // 同上，如果需要处理双箱子，需要修改函数签名或在调用处获取 BlockSource。
    }
    return success;
}

std::vector<std::string> getSharedPlayers(BlockPos pos, int dimId) {
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    std::vector<std::vector<std::string>> results = db.query(
        "SELECT player_uuid FROM shared_chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        static_cast<int>(dimId),
        pos.x,
        pos.y,
        pos.z
    );

    std::vector<std::string> sharedPlayers;
    for (const auto& row : results) {
        if (!row.empty()) {
            sharedPlayers.push_back(row[0]);
        }
    }
    return sharedPlayers;
}

} // namespace CT
