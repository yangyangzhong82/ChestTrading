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
    logger.debug("getChestDetails: Checking chest at pos ({}, {}, {}), dimId {}.", pos.x, pos.y, pos.z, dimId);
    
    // 首先尝试从缓存获取
    ChestCacheManager& cacheManager = ChestCacheManager::getInstance();
    ChestCacheEntry cacheEntry;
    
    if (cacheManager.getCachedChestInfo(pos, dimId, cacheEntry)) {
        logger.debug("getChestDetails: Cache hit for pos ({}, {}, {})", pos.x, pos.y, pos.z);
        return {cacheEntry.isLocked, cacheEntry.ownerUuid, cacheEntry.chestType};
    }
    
    logger.debug("getChestDetails: Cache miss, querying database");
    
    Sqlite3Wrapper&                       db      = Sqlite3Wrapper::getInstance();
    std::vector<std::vector<std::string>> results = db.query(
        "SELECT player_uuid, type FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    bool        isLocked  = false;
    std::string ownerUuid = "";
    ChestType   chestType = ChestType::Locked;

    if (!results.empty() && results[0].size() >= 2) {
        isLocked  = true;
        ownerUuid = results[0][0];
        chestType = static_cast<ChestType>(std::stoi(results[0][1]));
        logger.debug(
            "getChestDetails: Found primary chest part. Owner: {}, Type: {}.",
            ownerUuid,
            static_cast<int>(chestType)
        );
    } else {
        logger.debug("getChestDetails: Primary chest part not found in DB.");
    }

    // 检查是否是双箱子
    auto* blockActor = region.getBlockEntity(pos);
    if (blockActor && blockActor->mType == BlockActorType::Chest) {
        auto chest = static_cast<class ChestBlockActor*>(blockActor);
        if (chest->mLargeChestPaired) {
            BlockPos pairedChestPos = chest->mLargeChestPairedPosition;
            logger.debug("getChestDetails: Chest is large. Checking paired chest at ({}, {}, {}).", pairedChestPos.x, pairedChestPos.y, pairedChestPos.z);
            
            // 也尝试从缓存获取配对箱子信息
            ChestCacheEntry pairedCacheEntry;
            if (cacheManager.getCachedChestInfo(pairedChestPos, dimId, pairedCacheEntry)) {
                if (pairedCacheEntry.isLocked) {
                    isLocked  = true;
                    ownerUuid = pairedCacheEntry.ownerUuid;
                    chestType = pairedCacheEntry.chestType;
                    logger.debug("getChestDetails: Found paired chest in cache. Owner: {}, Type: {}.", ownerUuid, static_cast<int>(chestType));
                }
            } else {
                std::vector<std::vector<std::string>> pairedResults = db.query(
                    "SELECT player_uuid, type FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
                    dimId,
                    pairedChestPos.x,
                    pairedChestPos.y,
                    pairedChestPos.z
                );

                if (!pairedResults.empty() && pairedResults[0].size() >= 2) {
                    isLocked  = true;
                    ownerUuid = pairedResults[0][0];
                    chestType = static_cast<ChestType>(std::stoi(pairedResults[0][1]));
                    logger.debug(
                        "getChestDetails: Found paired chest part. Overriding with Owner: {}, Type: {}.",
                        ownerUuid,
                        static_cast<int>(chestType)
                    );
                    
                    // 缓存配对箱子信息
                    cacheManager.setCachedChestInfo(pairedChestPos, dimId, ChestCacheEntry(isLocked, ownerUuid, chestType));
                } else {
                    logger.debug("getChestDetails: Paired chest part not found in DB.");
                }
            }
        }
    }
    
    // 缓存查询结果
    cacheManager.setCachedChestInfo(pos, dimId, ChestCacheEntry(isLocked, ownerUuid, chestType));
    
    logger.debug(
        "getChestDetails: Final result - isLocked: {}, ownerUuid: '{}', chestType: {}.",
        isLocked,
        ownerUuid,
        static_cast<int>(chestType)
    );
    return {isLocked, ownerUuid, chestType};
}

bool setChest(const std::string& player_uuid, BlockPos pos, int dimId, BlockSource& region, ChestType type) {
    Sqlite3Wrapper& db      = Sqlite3Wrapper::getInstance();
    bool            success = db.execute(
        "INSERT OR REPLACE INTO chests (player_uuid, dim_id, pos_x, pos_y, pos_z, type) VALUES (?, ?, ?, ?, ?, ?);",
        player_uuid,
        dimId,
        pos.x,
        pos.y,
        pos.z,
        static_cast<int>(type)
    );

    if (success) {
        // 使缓存失效
        ChestCacheManager& cacheManager = ChestCacheManager::getInstance();
        cacheManager.invalidateCache(pos, dimId);
        
        // 根据箱子类型生成悬浮字文本
        std::string text;
        std::string ownerName = player_uuid; // 默认使用 UUID
        auto        playerInfo =
            ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(player_uuid));
        if (playerInfo) {
            ownerName = playerInfo->name;
        }

        switch (type) {
        case ChestType::Locked:
            text = "§e[上锁箱子]§r 拥有者: " + ownerName;
            break;
        case ChestType::RecycleShop:
            text = "§a[回收商店]§r 拥有者: " + ownerName;
            break;
        case ChestType::Shop:
            text = "§b[商店箱子]§r 拥有者: " + ownerName;
            break;
        case ChestType::Public:
            text = "§d[公共箱子]§r 拥有者: " + ownerName;
            break;
        default:
            text = "§f[未知箱子类型]§r 拥有者: " + ownerName;
            break;
        }
        FloatingTextManager::getInstance().addOrUpdateFloatingText(pos, dimId, player_uuid, text, type);

        // 如果是商店或回收商店，更新 FloatingTextManager 中的物品列表
        if (type == ChestType::Shop || type == ChestType::RecycleShop) {
            FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, type);
        }

        auto* blockActor = region.getBlockEntity(pos);
        if (blockActor) {
            auto chest = static_cast<class ChestBlockActor*>(blockActor);
            if (chest->mLargeChestPaired) {
                BlockPos pairedChestPos = chest->mLargeChestPairedPosition;
                db.execute(
                    "INSERT OR REPLACE INTO chests (player_uuid, dim_id, pos_x, pos_y, pos_z, type) VALUES (?, ?, ?, "
                    "?, ?, ?);",
                    player_uuid,
                    dimId,
                    pairedChestPos.x,
                    pairedChestPos.y,
                    pairedChestPos.z,
                    static_cast<int>(type)
                );
                FloatingTextManager::getInstance().addOrUpdateFloatingText(pairedChestPos, dimId, player_uuid, text, type);
                
                // 使配对箱子的缓存失效
                cacheManager.invalidateCache(pairedChestPos, dimId);

                // 如果是商店或回收商店，更新配对箱子的物品列表
                if (type == ChestType::Shop || type == ChestType::RecycleShop) {
                    FloatingTextManager::getInstance().updateShopFloatingText(pairedChestPos, dimId, type);
                }
            }
        }
    }
    return success;
}

bool removeChest(BlockPos pos, int dimId, BlockSource& region) {
    Sqlite3Wrapper& db      = Sqlite3Wrapper::getInstance();
    bool            success = db.execute(
        "DELETE FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    if (success) {
        // 使缓存失效
        ChestCacheManager& cacheManager = ChestCacheManager::getInstance();
        cacheManager.invalidateCache(pos, dimId);
        
        FloatingTextManager::getInstance().removeFloatingText(pos, dimId);

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
                
                // 使配对箱子的缓存失效
                cacheManager.invalidateCache(pairedChestPos, dimId);
                
                FloatingTextManager::getInstance().removeFloatingText(pairedChestPos, dimId);
            }
        }
    }
    return success;
}

bool addSharedPlayer(const std::string& owner_uuid, const std::string& shared_player_uuid, BlockPos pos, int dimId, BlockSource* region) {
    Sqlite3Wrapper& db      = Sqlite3Wrapper::getInstance();
    bool            success = db.execute(
        "INSERT OR REPLACE INTO shared_chests (player_uuid, owner_uuid, dim_id, pos_x, pos_y, pos_z) VALUES (?, ?, ?, ?, ?, ?);",
        shared_player_uuid,
        owner_uuid,
        static_cast<int>(dimId),
        pos.x,
        pos.y,
        pos.z
    );

    if (success && region) {
        // 检查是否是双箱子，如果是，也为配对的箱子添加分享玩家
        auto* blockActor = region->getBlockEntity(pos);
        if (blockActor && blockActor->mType == BlockActorType::Chest) {
            auto chest = static_cast<class ChestBlockActor*>(blockActor);
            if (chest->mLargeChestPaired) {
                BlockPos pairedChestPos = chest->mLargeChestPairedPosition;
                db.execute(
                    "INSERT OR REPLACE INTO shared_chests (player_uuid, owner_uuid, dim_id, pos_x, pos_y, pos_z) VALUES (?, ?, ?, ?, ?, ?);",
                    shared_player_uuid,
                    owner_uuid,
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

bool removeSharedPlayer(const std::string& shared_player_uuid, BlockPos pos, int dimId, BlockSource* region) {
    Sqlite3Wrapper& db      = Sqlite3Wrapper::getInstance();
    bool            success = db.execute(
        "DELETE FROM shared_chests WHERE player_uuid = ? AND dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        shared_player_uuid,
        static_cast<int>(dimId),
        pos.x,
        pos.y,
        pos.z
    );

    if (success && region) {
        // 检查是否是双箱子，如果是，也移除配对箱子的分享玩家
        auto* blockActor = region->getBlockEntity(pos);
        if (blockActor && blockActor->mType == BlockActorType::Chest) {
            auto chest = static_cast<class ChestBlockActor*>(blockActor);
            if (chest->mLargeChestPaired) {
                BlockPos pairedChestPos = chest->mLargeChestPairedPosition;
                db.execute(
                    "DELETE FROM shared_chests WHERE player_uuid = ? AND dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
                    shared_player_uuid,
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

std::vector<std::pair<std::string, std::string>> getSharedPlayersWithOwner(BlockPos pos, int dimId) {
    Sqlite3Wrapper&                       db      = Sqlite3Wrapper::getInstance();
    std::vector<std::vector<std::string>> results = db.query(
        "SELECT player_uuid, owner_uuid FROM shared_chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        static_cast<int>(dimId),
        pos.x,
        pos.y,
        pos.z
    );

    std::vector<std::pair<std::string, std::string>> sharedPlayers;
    for (const auto& row : results) {
        if (row.size() >= 2) {
            sharedPlayers.push_back({row[0], row[1]});  // {player_uuid, owner_uuid}
        }
    }
    return sharedPlayers;
}

std::vector<std::string> getSharedPlayers(BlockPos pos, int dimId) {
    Sqlite3Wrapper&                       db      = Sqlite3Wrapper::getInstance();
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
