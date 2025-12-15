#include "interaction/chestprotect.h"
#include "Bedrock-Authority/permission/PermissionManager.h"
#include "Config/ConfigManager.h"
#include "Utils/NbtUtils.h"
#include "db/Sqlite3Wrapper.h"
#include "ll/api/service/PlayerInfo.h"
#include "logger.h"
#include "mc/nbt/StringTag.h"
#include "mc/platform/UUID.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"



namespace CT {

// ========== ChestCacheManager 实现 ==========

bool ChestCacheManager::getCachedChestInfo(BlockPos pos, int dimId, ChestCacheEntry& entry) {
    std::lock_guard<std::mutex> lock(mCacheMutex);

    PositionKey key{dimId, pos.x, pos.y, pos.z};
    auto        it = mCache.find(key);

    if (it == mCache.end()) {
        return false;
    }

    // 检查缓存是否过期
    auto now     = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count();

    if (elapsed > mCacheTimeoutSeconds.load(std::memory_order_relaxed)) {
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
    mCacheTimeoutSeconds.store(seconds, std::memory_order_relaxed);
    logger.info("箱子缓存超时时间已设置为 {} 秒", seconds);
}

void ChestCacheManager::cleanupExpiredCache() {
    std::lock_guard<std::mutex> lock(mCacheMutex);

    auto   now          = std::chrono::steady_clock::now();
    size_t removedCount = 0;

    for (auto it = mCache.begin(); it != mCache.end();) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count();
        if (elapsed > mCacheTimeoutSeconds.load(std::memory_order_relaxed)) {
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
    logger.trace(
        "getChestDetails: Checking chest at pos ({}, {}, {}), mainPos ({}, {}, {}), dimId {}.",
        pos.x,
        pos.y,
        pos.z,
        mainPos.x,
        mainPos.y,
        mainPos.z,
        dimId
    );

    // 首先尝试从缓存获取主箱子信息
    ChestCacheManager& cacheManager = ChestCacheManager::getInstance();
    ChestCacheEntry    cacheEntry;
    if (cacheManager.getCachedChestInfo(mainPos, dimId, cacheEntry)) {
        logger.trace("getChestDetails: Cache hit for mainPos ({}, {}, {})", mainPos.x, mainPos.y, mainPos.z);
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
        logger
            .debug("getChestDetails: Found chest in DB. Owner: {}, Type: {}.", ownerUuid, static_cast<int>(chestType));
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
    Sqlite3Wrapper&    db           = Sqlite3Wrapper::getInstance();
    ChestCacheManager& cacheManager = ChestCacheManager::getInstance();
    BlockPos           mainPos      = internal::GetMainChestPos(pos, region);

    // 开始事务
    db.beginTransaction();

    // 先删除可能存在的旧记录
    db.execute(
        "DELETE FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    // 检查是否为大箱子
    auto*    blockActor = region.getBlockEntity(pos);
    BlockPos pairedChestPos;
    bool     isLargeChest = false;
    if (blockActor && blockActor->mType == BlockActorType::Chest) {
        auto* chest = static_cast<class ChestBlockActor*>(blockActor);
        if (chest->mLargeChestPaired) {
            isLargeChest   = true;
            pairedChestPos = chest->mLargeChestPairedPosition;
            // 如果是大箱子，也要删除配对箱子位置的旧记录，以防万一
            db.execute(
                "DELETE FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
                dimId,
                pairedChestPos.x,
                pairedChestPos.y,
                pairedChestPos.z
            );
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

        // 设置/移除箱子名称
        auto* mainBlockActor = region.getBlockEntity(mainPos);
        if (mainBlockActor) {
            auto nbt = NbtUtils::getBlockEntityNbt(mainBlockActor);
            if (nbt) {
                if (type == ChestType::Locked || type == ChestType::Public) {
                    std::string chestTypeName = (type == ChestType::Locked) ? "的上锁箱子" : "的公共箱子";
                    std::string customName    = ownerName + chestTypeName;
                    (*nbt)["CustomName"]      = StringTag(customName);
                    NbtUtils::setBlockEntityNbt(mainBlockActor, *nbt);
                } else if (nbt->contains("CustomName")) {
                    nbt->erase("CustomName");
                    NbtUtils::setBlockEntityNbt(mainBlockActor, *nbt);
                }
            }
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
    Sqlite3Wrapper&    db           = Sqlite3Wrapper::getInstance();
    ChestCacheManager& cacheManager = ChestCacheManager::getInstance();
    BlockPos           mainPos      = internal::GetMainChestPos(pos, region);

    // 只删除主方块的记录
    bool success = db.execute(
        "DELETE FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        mainPos.x,
        mainPos.y,
        mainPos.z
    );

    if (success) {
        // 使所有相关位置的缓存失效
        cacheManager.invalidateCache(mainPos, dimId);
        if (pos != mainPos) {
            cacheManager.invalidateCache(pos, dimId);
        }

        // 移除NBT中的自定义名称
        auto* mainBlockActor = region.getBlockEntity(mainPos);
        if (mainBlockActor) {
            auto nbt = NbtUtils::getBlockEntityNbt(mainBlockActor);
            if (nbt && nbt->contains("CustomName")) {
                nbt->erase("CustomName");
                NbtUtils::setBlockEntityNbt(mainBlockActor, *nbt);
            }
        }

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

bool addSharedPlayer(
    const std::string& owner_uuid,
    const std::string& shared_player_uuid,
    BlockPos           pos,
    int                dimId,
    BlockSource*       region
) {
    if (!region) {
        return false; // 需要 region 来确定主箱子
    }
    Sqlite3Wrapper& db      = Sqlite3Wrapper::getInstance();
    BlockPos        mainPos = internal::GetMainChestPos(pos, *region);

    return db.execute(
        "INSERT OR REPLACE INTO shared_chests (player_uuid, owner_uuid, dim_id, pos_x, pos_y, pos_z) VALUES (?, ?, ?, "
        "?, ?, ?);",
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

std::vector<std::pair<std::string, std::string>>
getSharedPlayersWithOwner(BlockPos pos, int dimId, BlockSource& region) {
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

bool canPlayerOpenChest(const std::string& player_uuid, BlockPos pos, int dimId, BlockSource& region) {
    auto [isLocked, ownerUuid, chestType] = getChestDetails(pos, dimId, region);

    // 如果箱子没有被锁定，或者是一个公共箱子，任何人都可以打开
    if (!isLocked || chestType == ChestType::Public) {
        return true;
    }

    // 如果玩家是箱子的主人，可以打开
    if (ownerUuid == player_uuid) {
        return true;
    }

    // 如果箱子是普通上锁类型，检查分享列表
    if (chestType == ChestType::Locked) {
        auto sharedPlayers = getSharedPlayers(pos, dimId, region);
        for (const auto& sharedPlayerUuid : sharedPlayers) {
            if (sharedPlayerUuid == player_uuid) {
                return true; // 玩家在分享列表中
            }
        }
    }

    // 对于商店和回收商店，只有主人能从“后端”打开（即直接交互）
    // 游客只能通过表单交互，这里的检查是针对直接打开箱子的行为

    return false; // 默认情况下，如果以上条件都不满足，则不能打开
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
            if (pos.x < pairedPos.x
                || (pos.x == pairedPos.x && (pos.y < pairedPos.y || (pos.y == pairedPos.y && pos.z < pairedPos.z)))) {
                return pos; // 当前方块是主方块
            } else {
                return pairedPos; // 配对的方块是主方块
            }
        }
    }
    return pos; // 单箱子或非箱子实体
}
} // namespace internal

std::vector<ChestInfo> getAllChests() {
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    auto results = db.query("SELECT dim_id, pos_x, pos_y, pos_z, player_uuid, type, shop_name, enable_floating_text, "
                            "enable_fake_item, is_public FROM chests ORDER BY player_uuid, dim_id;");

    std::vector<ChestInfo> chests;
    for (const auto& row : results) {
        if (row.size() >= 6) {
            try {
                ChestInfo info;
                info.dimId              = std::stoi(row[0]);
                info.pos                = BlockPos{std::stoi(row[1]), std::stoi(row[2]), std::stoi(row[3])};
                info.ownerUuid          = row[4];
                info.type               = static_cast<ChestType>(std::stoi(row[5]));
                info.shopName           = (row.size() >= 7) ? row[6] : "";
                info.enableFloatingText = (row.size() >= 8) ? (std::stoi(row[7]) != 0) : true;
                info.enableFakeItem     = (row.size() >= 9) ? (std::stoi(row[8]) != 0) : true;
                info.isPublic           = (row.size() >= 10) ? (std::stoi(row[9]) != 0) : true;
                chests.push_back(info);
            } catch (const std::exception& e) {
                logger.error("Failed to parse chest info from database: {}", e.what());
            }
        }
    }
    return chests;
}

std::string getShopName(BlockPos pos, int dimId, BlockSource& region) {
    BlockPos        mainPos = internal::GetMainChestPos(pos, region);
    Sqlite3Wrapper& db      = Sqlite3Wrapper::getInstance();
    auto            results = db.query(
        "SELECT shop_name FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        dimId,
        mainPos.x,
        mainPos.y,
        mainPos.z
    );
    if (!results.empty() && !results[0].empty()) {
        return results[0][0];
    }
    return "";
}

bool setShopName(BlockPos pos, int dimId, BlockSource& region, const std::string& shopName) {
    BlockPos        mainPos = internal::GetMainChestPos(pos, region);
    Sqlite3Wrapper& db      = Sqlite3Wrapper::getInstance();
    return db.execute(
        "UPDATE chests SET shop_name = ? WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
        shopName,
        dimId,
        mainPos.x,
        mainPos.y,
        mainPos.z
    );
}

ChestConfig getChestConfig(BlockPos pos, int dimId, BlockSource& region) {
    BlockPos        mainPos = internal::GetMainChestPos(pos, region);
    Sqlite3Wrapper& db      = Sqlite3Wrapper::getInstance();
    auto            results = db.query(
        "SELECT enable_floating_text, enable_fake_item, is_public FROM chests WHERE dim_id = ? AND pos_x = ? AND pos_y "
                   "= ? AND pos_z = ?;",
        dimId,
        mainPos.x,
        mainPos.y,
        mainPos.z
    );
    ChestConfig config;
    if (!results.empty() && results[0].size() >= 3) {
        config.enableFloatingText = (std::stoi(results[0][0]) != 0);
        config.enableFakeItem     = (std::stoi(results[0][1]) != 0);
        config.isPublic           = (std::stoi(results[0][2]) != 0);
    }
    return config;
}

bool setChestConfig(BlockPos pos, int dimId, BlockSource& region, const ChestConfig& config) {
    BlockPos        mainPos = internal::GetMainChestPos(pos, region);
    Sqlite3Wrapper& db      = Sqlite3Wrapper::getInstance();
    bool            success = db.execute(
        "UPDATE chests SET enable_floating_text = ?, enable_fake_item = ?, is_public = ? WHERE dim_id = ? AND pos_x = "
                   "? AND pos_y = ? AND pos_z = ?;",
        config.enableFloatingText ? 1 : 0,
        config.enableFakeItem ? 1 : 0,
        config.isPublic ? 1 : 0,
        dimId,
        mainPos.x,
        mainPos.y,
        mainPos.z
    );
    if (success) {
        // 根据配置更新悬浮字显示
        auto& ftm = FloatingTextManager::getInstance();
        if (!config.enableFloatingText) {
            ftm.removeFloatingText(mainPos, dimId);
        } else {
            // 重新加载悬浮字
            auto [isLocked, ownerUuid, chestType] = getChestDetails(pos, dimId, region);
            if (isLocked) {
                std::string ownerName = ownerUuid;
                if (auto playerInfo =
                        ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(ownerUuid))) {
                    ownerName = playerInfo->name;
                }
                std::string text;
                switch (chestType) {
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
                ftm.addOrUpdateFloatingText(mainPos, dimId, ownerUuid, text, chestType);
                if (chestType == ChestType::Shop || chestType == ChestType::RecycleShop) {
                    ftm.updateShopFloatingText(mainPos, dimId, chestType);
                }
            }
        }
    }
    return success;
}

int getPlayerChestCount(const std::string& playerUuid, ChestType type) {
    Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
    auto            results =
        db.query("SELECT COUNT(*) FROM chests WHERE player_uuid = ? AND type = ?;", playerUuid, static_cast<int>(type));

    if (!results.empty() && !results[0].empty()) {
        try {
            return std::stoi(results[0][0]);
        } catch (const std::exception& e) {
            logger.error("Failed to parse chest count for player {}: {}", playerUuid, e.what());
            return 0;
        }
    }
    return 0;
}

bool canPlayerCreateChest(const std::string& playerUuid, ChestType type, std::string& errorMessage) {
    // 检查管理员权限
    bool isAdmin = BA::permission::PermissionManager::getInstance().hasPermission(playerUuid, "chest.admin");
    if (isAdmin) {
        return true; // 管理员绕过所有限制
    }

    // 检查对应权限
    std::string requiredPermission;
    switch (type) {
    case ChestType::Locked:
        requiredPermission = "chest.create.locked";
        break;
    case ChestType::Public:
        requiredPermission = "chest.create.public";
        break;
    case ChestType::RecycleShop:
        requiredPermission = "chest.create.recycle";
        break;
    case ChestType::Shop:
        requiredPermission = "chest.create.shop";
        break;
    default:
        errorMessage = "§c未知的箱子类型！";
        return false;
    }

    if (!BA::permission::PermissionManager::getInstance().hasPermission(playerUuid, requiredPermission)) {
        errorMessage = "§c你没有创建该类型箱子的权限！";
        return false;
    }

    // 检查数量限制
    const Config& config       = ConfigManager::getInstance().get();
    int           currentCount = getPlayerChestCount(playerUuid, type);
    int           maxCount     = 0;
    std::string   chestTypeName;

    switch (type) {
    case ChestType::Locked:
        maxCount      = config.chestLimits.maxLockedChests;
        chestTypeName = "上锁箱子";
        break;
    case ChestType::Public:
        maxCount      = config.chestLimits.maxPublicChests;
        chestTypeName = "公共箱子";
        break;
    case ChestType::RecycleShop:
        maxCount      = config.chestLimits.maxRecycleShops;
        chestTypeName = "回收商店";
        break;
    case ChestType::Shop:
        maxCount      = config.chestLimits.maxShops;
        chestTypeName = "商店";
        break;
    default:
        errorMessage = "§c未知的箱子类型！";
        return false;
    }

    if (currentCount >= maxCount) {
        errorMessage = "§c你已达到" + chestTypeName + "的数量上限（" + std::to_string(maxCount) + "个）！";
        return false;
    }

    return true;
}

} // namespace CT
