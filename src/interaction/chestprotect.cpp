#include "interaction/chestprotect.h"
#include "db/Sqlite3Wrapper.h" // 引入 Sqlite3Wrapper
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/PlayerInfo.h" // 引入 PlayerInfo
#include "logger.h"
#include "mc/platform/UUID.h"
#include "mc/world/level/BlockSource.h"                 // 引入 BlockSource
#include "mc/world/level/block/actor/ChestBlockActor.h" // 引入 ChestBlockActor
#include "mc/world/item/ItemStack.h"                    // 引入 ItemStack
#include "mc/nbt/CompoundTag.h"                         // 引入 CompoundTag


namespace CT {

// ========== ChestCacheManager 实现 ==========

bool ChestCacheManager::getCachedChestInfo(BlockPos pos, int dimId, ChestCacheEntry& entry) {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    
    PositionKey key{dimId, pos.x, pos.y, pos.z};
    auto it = mCache.find(key);
    
    if (it == mCache.end()) {
        return false;
    }
    
    // 检查缓存是否过期
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count();
    
    if (elapsed > mCacheTimeoutSeconds) {
        mCache.erase(it);
        return false;
    }
    
    entry = it->second;
    return true;
}

void ChestCacheManager::setCachedChestInfo(BlockPos pos, int dimId, const ChestCacheEntry& entry) {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    
    PositionKey key{dimId, pos.x, pos.y, pos.z};
    mCache[key] = entry;
}

void ChestCacheManager::invalidateCache(BlockPos pos, int dimId) {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    
    PositionKey key{dimId, pos.x, pos.y, pos.z};
    mCache.erase(key);
}

void ChestCacheManager::clearAllCache() {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    mCache.clear();
    logger.info("箱子缓存已清空");
}

void ChestCacheManager::setCacheTimeout(int seconds) {
    mCacheTimeoutSeconds = seconds;
    logger.info("箱子缓存超时时间已设置为 {} 秒", seconds);
}

void ChestCacheManager::cleanupExpiredCache() {
    std::lock_guard<std::mutex> lock(mCacheMutex);
    
    auto now = std::chrono::steady_clock::now();
    size_t removedCount = 0;
    
    for (auto it = mCache.begin(); it != mCache.end();) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count();
        if (elapsed > mCacheTimeoutSeconds) {
            it = mCache.erase(it);
            removedCount++;
        } else {
            ++it;
        }
    }
    
    if (removedCount > 0) {
        logger.debug("清理了 {} 个过期的箱子缓存条目", removedCount);
    }
}

// ========== 箱子保护功能实现 ==========


std::tuple<bool, std::string, ChestType> getChestDetails(BlockPos pos, int dimId, BlockSource& region) {
    BlockPos mainPos = internal::GetMainChestPos(pos, region);
    logger.debug("getChestDetails: Checking chest at pos ({}, {}, {}), mainPos ({}, {}, {}), dimId {}.", pos.x, pos.y, pos.z, mainPos.x, mainPos.y, mainPos.z, dimId);

    // 首先尝试从缓存获取主箱子信息
    ChestCacheManager& cacheManager = ChestCacheManager::getInstance();
    ChestCacheEntry    cacheEntry;
    if (cacheManager.getCachedChestInfo(mainPos, dimId, cacheEntry)) {
        logger.debug("getChestDetails: Cache hit for mainPos ({}, {}, {})", mainPos.x, mainPos.y, mainPos.z);
        // 也为当前查询的pos缓存结果，避免下次重复计算mainPos
        if (pos != mainPos) {
            cacheManager.setCachedChestInfo(pos, dimId, cacheEntry);
        }
        return {cacheEntry.isLocked, cacheEntry.ownerUuid, cacheEntry.chestType};
    }

    logger.debug("getChestDetails: Cache miss for mainPos, querying database");

    Sqlite3Wrapper&                       db      = Sqlite3Wrapper::getInstance();
    std::vector<std::vector<std::string>> results = db.query(
        "SELECT player_uuid, type FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        mainPos.x,
        mainPos.y,
        mainPos.z
    );

    bool        isLocked  = false;
    std::string ownerUuid = "";
    ChestType   chestType = ChestType::Locked;

    if (!results.empty() && results[0].size() >= 2) {
        isLocked  = true;
        ownerUuid = results[0][0];
        chestType = static_cast<ChestType>(std::stoi(results[0][1]));
        logger.debug(
            "getChestDetails: Found chest in DB. Owner: {}, Type: {}.",
            ownerUuid,
            static_cast<int>(chestType)
        );
    } else {
        logger.debug("getChestDetails: Chest not found in DB.");
    }

    // 缓存查询结果
    ChestCacheEntry newEntry(isLocked, ownerUuid, chestType);
    cacheManager.setCachedChestInfo(mainPos, dimId, newEntry);
    if (pos != mainPos) {
        cacheManager.setCachedChestInfo(pos, dimId, newEntry);
    }

    logger.debug(
        "getChestDetails: Final result - isLocked: {}, ownerUuid: '{}', chestType: {}.",
        isLocked,
        ownerUuid,
        static_cast<int>(chestType)
    );
    return {isLocked, ownerUuid, chestType};
}

bool setChest(const std::string& player_uuid, BlockPos pos, int dimId, BlockSource& region, ChestType type) {
    Sqlite3Wrapper&    db         = Sqlite3Wrapper::getInstance();
    ChestCacheManager& cacheManager = ChestCacheManager::getInstance();
    BlockPos           mainPos    = internal::GetMainChestPos(pos, region);

    // 开始事务
    db.beginTransaction();

    // 先删除可能存在的旧记录
    db.execute("DELETE FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;", dimId, pos.x, pos.y, pos.z);
    
    // 检查是否为大箱子
    auto* blockActor = region.getBlockEntity(pos);
    BlockPos pairedChestPos;
    bool isLargeChest = false;
    if (blockActor && blockActor->mType == BlockActorType::Chest) {
        auto* chest = static_cast<class ChestBlockActor*>(blockActor);
        if (chest->mLargeChestPaired) {
            isLargeChest = true;
            pairedChestPos = chest->mLargeChestPairedPosition;
            // 如果是大箱子，也要删除配对箱子位置的旧记录，以防万一
             db.execute("DELETE FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;", dimId, pairedChestPos.x, pairedChestPos.y, pairedChestPos.z);
        }
    }

    // 只在主方块位置插入/替换记录
    bool success = db.execute(
        "INSERT OR REPLACE INTO chests (player_uuid, dim_id, pos_x, pos_y, pos_z, type) VALUES (?, ?, ?, ?, ?, ?);",
        player_uuid,
        dimId,
        mainPos.x,
        mainPos.y,
        mainPos.z,
        static_cast<int>(type)
    );

    if (success) {
        // 提交事务
        db.commit();

        // 使缓存失效
        cacheManager.invalidateCache(pos, dimId);
        if (isLargeChest) {
            cacheManager.invalidateCache(pairedChestPos, dimId);
        }
        
        // 生成悬浮字文本
        std::string text;
        std::string ownerName = player_uuid;
        if (auto playerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(player_uuid))) {
            ownerName = playerInfo->name;
        }

        switch (type) {
            case ChestType::Locked:      text = "§e[上锁箱子]§r 拥有者: " + ownerName; break;
            case ChestType::RecycleShop: text = "§a[回收商店]§r 拥有者: " + ownerName; break;
            case ChestType::Shop:        text = "§b[商店箱子]§r 拥有者: " + ownerName; break;
            case ChestType::Public:      text = "§d[公共箱子]§r 拥有者: " + ownerName; break;
            default:                     text = "§f[未知箱子类型]§r 拥有者: " + ownerName; break;
        }

        // 统一在主方块位置创建悬浮字
        FloatingTextManager::getInstance().addOrUpdateFloatingText(mainPos, dimId, player_uuid, text, type);
        // 如果是大箱子，确保另一个方块上没有悬浮字
        if (isLargeChest) {
            BlockPos otherPos = (mainPos == pos) ? pairedChestPos : pos;
            FloatingTextManager::getInstance().removeFloatingText(otherPos, dimId);
        }

        // 如果是商店，更新主方塊的悬浮字物品列表
        if (type == ChestType::Shop || type == ChestType::RecycleShop) {
            FloatingTextManager::getInstance().updateShopFloatingText(mainPos, dimId, type);
        }
    } else {
        // 回滚事务
        db.rollback();
    }
    
    return success;
}

bool removeChest(BlockPos pos, int dimId, BlockSource& region) {
    Sqlite3Wrapper&    db         = Sqlite3Wrapper::getInstance();
    ChestCacheManager& cacheManager = ChestCacheManager::getInstance();
    BlockPos           mainPos    = internal::GetMainChestPos(pos, region);

    // 只删除主方块的记录
    bool success = db.execute(
        "DELETE FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        mainPos.x,
        mainPos.y,
        mainPos.z
    );

    if (success) {
        // 使缓存失效
        cacheManager.invalidateCache(pos, dimId);
        
        // 只移除主方块的悬浮字
        FloatingTextManager::getInstance().removeFloatingText(mainPos, dimId);

        // 如果是大箱子，也要让配对的方块缓存失效
        auto* blockActor = region.getBlockEntity(pos);
        if (blockActor && blockActor->mType == BlockActorType::Chest) {
            auto* chest = static_cast<class ChestBlockActor*>(blockActor);
            if (chest->mLargeChestPaired) {
                BlockPos pairedChestPos = chest->mLargeChestPairedPosition;
                cacheManager.invalidateCache(pairedChestPos, dimId);
            }
        }
    }
    return success;
}

bool addSharedPlayer(const std::string& owner_uuid, const std::string& shared_player_uuid, BlockPos pos, int dimId, BlockSource* region) {
    if (!region) {
        return false; // 需要 region 来确定主箱子
    }
    Sqlite3Wrapper& db      = Sqlite3Wrapper::getInstance();
    BlockPos        mainPos = internal::GetMainChestPos(pos, *region);

    return db.execute(
        "INSERT OR REPLACE INTO shared_chests (player_uuid, owner_uuid, dim_id, pos_x, pos_y, pos_z) VALUES (?, ?, ?, ?, ?, ?);",
        shared_player_uuid,
        owner_uuid,
        dimId,
        mainPos.x,
        mainPos.y,
        mainPos.z
    );
}

bool removeSharedPlayer(const std::string& shared_player_uuid, BlockPos pos, int dimId, BlockSource* region) {
    if (!region) {
        return false; // 需要 region 来确定主箱子
    }
    Sqlite3Wrapper& db      = Sqlite3Wrapper::getInstance();
    BlockPos        mainPos = internal::GetMainChestPos(pos, *region);

    return db.execute(
        "DELETE FROM shared_chests WHERE player_uuid = ? AND dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        shared_player_uuid,
        dimId,
        mainPos.x,
        mainPos.y,
        mainPos.z
    );
}

std::vector<std::pair<std::string, std::string>> getSharedPlayersWithOwner(BlockPos pos, int dimId, BlockSource& region) {
    Sqlite3Wrapper& db      = Sqlite3Wrapper::getInstance();
    BlockPos        mainPos = internal::GetMainChestPos(pos, region);

    std::vector<std::vector<std::string>> results = db.query(
        "SELECT player_uuid, owner_uuid FROM shared_chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        mainPos.x,
        mainPos.y,
        mainPos.z
    );

    std::vector<std::pair<std::string, std::string>> sharedPlayers;
    for (const auto& row : results) {
        if (row.size() >= 2) {
            sharedPlayers.push_back({row[0], row[1]}); // {player_uuid, owner_uuid}
        }
    }
    return sharedPlayers;
}

std::vector<std::string> getSharedPlayers(BlockPos pos, int dimId, BlockSource& region) {
    Sqlite3Wrapper& db      = Sqlite3Wrapper::getInstance();
    BlockPos        mainPos = internal::GetMainChestPos(pos, region);

    std::vector<std::vector<std::string>> results = db.query(
        "SELECT player_uuid FROM shared_chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        mainPos.x,
        mainPos.y,
        mainPos.z
    );

    std::vector<std::string> sharedPlayers;
    for (const auto& row : results) {
        if (!row.empty()) {
            sharedPlayers.push_back(row[0]);
        }
    }
    return sharedPlayers;
}

namespace internal {
BlockPos GetMainChestPos(BlockPos pos, BlockSource& region) {
    auto* blockActor = region.getBlockEntity(pos);
    if (blockActor && blockActor->mType == BlockActorType::Chest) {
        auto* chest = static_cast<class ChestBlockActor*>(blockActor);
        if (chest->mLargeChestPaired) {
            BlockPos pairedPos = chest->mLargeChestPairedPosition;
            // 通过比较坐标来确定一个唯一的“主”方块
            // 目的是无论玩家与大箱子的哪一半交互，我们都能定位到同一个数据库条目
            if (pos.x < pairedPos.x || (pos.x == pairedPos.x && (pos.y < pairedPos.y || (pos.y == pairedPos.y && pos.z < pairedPos.z)))) {
                return pos; // 当前方块是主方块
            } else {
                return pairedPos; // 配对的方块是主方块
            }
        }
    }
    return pos; // 单箱子或非箱子实体
}
} // namespace internal


} // namespace CT
