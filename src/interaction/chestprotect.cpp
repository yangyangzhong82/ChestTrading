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

} // namespace CT
