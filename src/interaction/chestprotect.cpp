#include "interaction/chestprotect.h"
#include "ll/api/form/SimpleForm.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "db/Sqlite3Wrapper.h" // 引入 Sqlite3Wrapper
#include "mc/world/level/BlockSource.h" // 引入 BlockSource
#include "mc/world/level/block/actor/ChestBlockActor.h" // 引入 ChestBlockActor

namespace CT {



std::pair<bool, std::string> isChestLocked(BlockPos pos, int dimId) {
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    std::vector<std::vector<std::string>> results = db.query(
        "SELECT player_uuid FROM locked_chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        static_cast<int>(dimId),
        pos.x,
        pos.y,
        pos.z
    );

    if (!results.empty()) {
        return {true, results[0][0]}; // 箱子被锁定，返回主人UUID
    }
    return {false, ""}; // 箱子未被锁定
}

bool lockChest(const std::string& player_uuid, BlockPos pos, int dimId, BlockSource& region) {
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    bool success = db.execute(
        "INSERT OR REPLACE INTO locked_chests (player_uuid, dim_id, pos_x, pos_y, pos_z) VALUES (?, ?, ?, ?, ?);",
        player_uuid,
        static_cast<int>(dimId),
        pos.x,
        pos.y,
        pos.z
    );

    if (success) {
        auto* blockActor = region.getBlockEntity(pos);
        if (blockActor) { // 检查 blockActor 是否存在
            auto chest = static_cast<class ChestBlockActor*>(blockActor); // 假设它是 ChestBlockActor
            if (chest->mLargeChestPaired) { // 检查是否是双箱子
                // 如果是双箱子，也锁定配对的箱子
                BlockPos pairedChestPos = chest->mLargeChestPairedPosition;
                db.execute(
                    "INSERT OR REPLACE INTO locked_chests (player_uuid, dim_id, pos_x, pos_y, pos_z) VALUES (?, ?, ?, ?, ?);",
                    player_uuid,
                    static_cast<int>(dimId),
                    pairedChestPos.x,
                    pairedChestPos.y,
                    pairedChestPos.z
                );
            }
        }
    }
    return success;
}

bool unlockChest(BlockPos pos, int dimId, BlockSource& region) {
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    bool success = db.execute(
        "DELETE FROM locked_chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        static_cast<int>(dimId),
        pos.x,
        pos.y,
        pos.z
    );

    if (success) {
        auto* blockActor = region.getBlockEntity(pos);
        if (blockActor) { // 检查 blockActor 是否存在
            auto chest = static_cast<class ChestBlockActor*>(blockActor); // 假设它是 ChestBlockActor
            if (chest->mLargeChestPaired) { // 检查是否是双箱子
                // 如果是双箱子，也解锁配对的箱子
                BlockPos pairedChestPos = chest->mLargeChestPairedPosition;
                db.execute(
                    "DELETE FROM locked_chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
                    static_cast<int>(dimId),
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
