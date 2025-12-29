#include "ChestService.h"
#include "Bedrock-Authority/permission/PermissionManager.h"
#include "Config/ConfigManager.h"
#include "FloatingText/FloatingText.h"
#include "TextService.h"
#include "Utils/NbtUtils.h"
#include "db/Sqlite3Wrapper.h"
#include "ll/api/service/PlayerInfo.h"
#include "logger.h"
#include "mc/nbt/StringTag.h"
#include "mc/platform/UUID.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "repository/ChestRepository.h"

namespace CT {

ChestService& ChestService::getInstance() {
    static ChestService instance;
    return instance;
}

BlockPos ChestService::getMainChestPos(BlockPos pos, BlockSource& region) {
    auto* blockActor = region.getBlockEntity(pos);
    if (!blockActor || blockActor->mType != BlockActorType::Chest) {
        return pos;
    }
    auto* chest = static_cast<ChestBlockActor*>(blockActor);
    if (!chest->mLargeChestPaired) {
        return pos;
    }
    BlockPos pairedPos = chest->mLargeChestPairedPosition;
    // 返回坐标较小的位置作为主位置
    if (pos.x < pairedPos.x || (pos.x == pairedPos.x && pos.z < pairedPos.z)) {
        return pos;
    }
    return pairedPos;
}

// === ChestCacheManager 实现 ===

bool ChestCacheManager::getCachedChestInfo(BlockPos pos, int dimId, ChestCacheEntry& entry) {
    std::shared_lock lock(mCacheMutex);
    PositionKey      key{dimId, pos.x, pos.y, pos.z};
    auto             it = mCache.find(key);
    if (it == mCache.end()) return false;

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
    std::unique_lock lock(mCacheMutex);
    PositionKey      key{dimId, pos.x, pos.y, pos.z};
    mCache[key] = entry;
}

void ChestCacheManager::invalidateCache(BlockPos pos, int dimId) {
    std::unique_lock lock(mCacheMutex);
    PositionKey      key{dimId, pos.x, pos.y, pos.z};
    mCache.erase(key);
}

void ChestCacheManager::clearAllCache() {
    std::unique_lock lock(mCacheMutex);
    mCache.clear();
}

void ChestCacheManager::setCacheTimeout(int seconds) { mCacheTimeoutSeconds.store(seconds, std::memory_order_relaxed); }

void ChestCacheManager::cleanupExpiredCache() {
    std::unique_lock lock(mCacheMutex);
    auto             now = std::chrono::steady_clock::now();
    for (auto it = mCache.begin(); it != mCache.end();) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count();
        if (elapsed > mCacheTimeoutSeconds.load(std::memory_order_relaxed)) {
            it = mCache.erase(it);
        } else {
            ++it;
        }
    }
}

ChestOperationResult
ChestService::createChest(const std::string& playerUuid, BlockPos pos, int dimId, ChestType type, BlockSource& region) {
    BlockPos mainPos = getMainChestPos(pos, region);

    // 检查是否为大箱子
    auto*    blockActor = region.getBlockEntity(pos);
    BlockPos pairedChestPos;
    bool     isLargeChest = false;
    if (blockActor && blockActor->mType == BlockActorType::Chest) {
        auto* chest = static_cast<ChestBlockActor*>(blockActor);
        if (chest->mLargeChestPaired) {
            isLargeChest   = true;
            pairedChestPos = chest->mLargeChestPairedPosition;
        }
    }

    auto& repo = ChestRepository::getInstance();
    auto& db   = Sqlite3Wrapper::getInstance();

    // 使用事务保护
    auto&       txt = TextService::getInstance();
    Transaction txn(db);
    if (!txn.isActive()) {
        return {false, txt.getMessage("chest.set_fail_transaction"), std::nullopt};
    }

    // 删除旧记录
    repo.remove(pos, dimId);
    if (isLargeChest) {
        repo.remove(pairedChestPos, dimId);
    }

    // 创建新记录
    ChestData data;
    data.ownerUuid = playerUuid;
    data.dimId     = dimId;
    data.pos       = mainPos;
    data.type      = type;

    if (!repo.insert(data)) {
        return {false, txt.getMessage("chest.set_fail"), std::nullopt};
    }

    if (!txn.commit()) {
        return {false, txt.getMessage("chest.set_fail_commit"), std::nullopt};
    }

    // 使缓存失效
    ChestCacheManager::getInstance().invalidateCache(pos, dimId);
    if (isLargeChest) {
        ChestCacheManager::getInstance().invalidateCache(pairedChestPos, dimId);
    }

    // 更新悬浮字
    updateFloatingText(mainPos, dimId, playerUuid, type);

    // 如果是大箱子，移除另一个方块的悬浮字
    if (isLargeChest) {
        BlockPos otherPos = (mainPos == pos) ? pairedChestPos : pos;
        FloatingTextManager::getInstance().removeFloatingText(otherPos, dimId);
    }

    // 更新商店悬浮字
    if (type == ChestType::Shop || type == ChestType::RecycleShop) {
        FloatingTextManager::getInstance().updateShopFloatingText(mainPos, dimId, type);
    }

    // 设置箱子名称
    auto* mainBlockActor = region.getBlockEntity(mainPos);
    if (mainBlockActor && mainBlockActor->mType == BlockActorType::Chest) {
        auto nbt = NbtUtils::getBlockEntityNbt(mainBlockActor);
        if (nbt) {
            std::string ownerName = playerUuid;
            if (auto info = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(playerUuid))) {
                ownerName = info->name;
            }

            if (type == ChestType::Locked || type == ChestType::Public) {
                std::string chestTypeName = (type == ChestType::Locked) ? txt.getMessage("chest.name_locked")
                                                                        : txt.getMessage("chest.name_public");
                (*nbt)["CustomName"]      = StringTag(ownerName + chestTypeName);
                NbtUtils::setBlockEntityNbt(mainBlockActor, *nbt);
            }
        }
    }

    return {true, txt.getMessage("chest.set_success"), data};
}

ChestOperationResult ChestService::removeChest(BlockPos pos, int dimId, BlockSource& region) {
    BlockPos mainPos = getMainChestPos(pos, region);
    auto&    repo    = ChestRepository::getInstance();

    if (!repo.remove(mainPos, dimId)) {
        return {false, TextService::getInstance().getMessage("chest.remove_fail_db"), std::nullopt};
    }

    // 使缓存失效
    ChestCacheManager::getInstance().invalidateCache(mainPos, dimId);
    if (pos != mainPos) {
        ChestCacheManager::getInstance().invalidateCache(pos, dimId);
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

    // 移除悬浮字
    FloatingTextManager::getInstance().removeFloatingText(mainPos, dimId);

    // 如果是大箱子，也使配对方块缓存失效
    auto* blockActor = region.getBlockEntity(pos);
    if (blockActor && blockActor->mType == BlockActorType::Chest) {
        auto* chest = static_cast<ChestBlockActor*>(blockActor);
        if (chest->mLargeChestPaired) {
            ChestCacheManager::getInstance().invalidateCache(chest->mLargeChestPairedPosition, dimId);
        }
    }

    return {true, TextService::getInstance().getMessage("chest.remove_success"), std::nullopt};
}

std::optional<ChestData> ChestService::getChestInfo(BlockPos pos, int dimId, BlockSource& region) {
    BlockPos mainPos = getMainChestPos(pos, region);

    // 先检查缓存
    ChestCacheEntry cacheEntry;
    if (ChestCacheManager::getInstance().getCachedChestInfo(mainPos, dimId, cacheEntry)) {
        if (!cacheEntry.isLocked) {
            return std::nullopt;
        }
        ChestData data;
        data.dimId     = dimId;
        data.pos       = mainPos;
        data.ownerUuid = cacheEntry.ownerUuid;
        data.type      = cacheEntry.chestType;
        return data;
    }

    // 从数据库查询
    auto result = ChestRepository::getInstance().findByPosition(mainPos, dimId);

    // 更新缓存
    if (result) {
        ChestCacheEntry entry(true, result->ownerUuid, result->type);
        ChestCacheManager::getInstance().setCachedChestInfo(mainPos, dimId, entry);
        if (pos != mainPos) {
            ChestCacheManager::getInstance().setCachedChestInfo(pos, dimId, entry);
        }
    } else {
        ChestCacheEntry entry(false, "", ChestType::Invalid);
        ChestCacheManager::getInstance().setCachedChestInfo(mainPos, dimId, entry);
    }

    return result;
}

bool ChestService::hasChestConfig(BlockPos pos, int dimId, BlockSource& region) {
    return getChestInfo(pos, dimId, region).has_value();
}

bool ChestService::isChestProtected(BlockPos pos, int dimId, BlockSource& region) {
    // 当前所有有配置的箱子都需要保护
    // 未来可以根据箱子类型或配置项进行更细粒度的控制
    auto info = getChestInfo(pos, dimId, region);
    if (!info) {
        return false;
    }

    // 目前所有类型的箱子都需要保护
    // 未来如果需要，可以根据 info->type 或其他配置决定是否保护
    return true;
}

bool ChestService::isChestLocked(BlockPos pos, int dimId, BlockSource& region) {
    // 为保持向后兼容，委托给 hasChestConfig
    return hasChestConfig(pos, dimId, region);
}

bool ChestService::isOwner(const std::string& playerUuid, BlockPos pos, int dimId, BlockSource& region) {
    auto info = getChestInfo(pos, dimId, region);
    return info && info->ownerUuid == playerUuid;
}

bool ChestService::canPlayerAccess(const std::string& playerUuid, BlockPos pos, int dimId, BlockSource& region) {
    auto info = getChestInfo(pos, dimId, region);

    // 未锁定或公共箱子
    if (!info || info->type == ChestType::Public) {
        return true;
    }

    // 是主人
    if (info->ownerUuid == playerUuid) {
        return true;
    }

    // 检查分享列表
    if (info->type == ChestType::Locked) {
        BlockPos mainPos = getMainChestPos(pos, region);
        if (ChestRepository::getInstance().isPlayerShared(playerUuid, mainPos, dimId)) {
            return true;
        }
    }

    return false;
}

bool ChestService::canPlayerCreateChest(const std::string& playerUuid, ChestType type, std::string& errorMessage) {
    // 检查管理员权限
    if (BA::permission::PermissionManager::getInstance().hasPermission(playerUuid, "chest.admin")) {
        return true;
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
        errorMessage = TextService::getInstance().getMessage("chest.unknown_type");
        return false;
    }

    if (!BA::permission::PermissionManager::getInstance().hasPermission(playerUuid, requiredPermission)) {
        errorMessage = TextService::getInstance().getMessage("chest.no_permission");
        return false;
    }

    // 检查数量限制
    const Config& config       = ConfigManager::getInstance().get();
    auto&         txt          = TextService::getInstance();
    int           currentCount = getPlayerChestCount(playerUuid, type);
    int           maxCount     = 0;
    std::string   chestTypeName;

    switch (type) {
    case ChestType::Locked:
        maxCount      = config.chestLimits.maxLockedChests;
        chestTypeName = txt.getChestTypeName(ChestType::Locked);
        break;
    case ChestType::Public:
        maxCount      = config.chestLimits.maxPublicChests;
        chestTypeName = txt.getChestTypeName(ChestType::Public);
        break;
    case ChestType::RecycleShop:
        maxCount      = config.chestLimits.maxRecycleShops;
        chestTypeName = txt.getChestTypeName(ChestType::RecycleShop);
        break;
    case ChestType::Shop:
        maxCount      = config.chestLimits.maxShops;
        chestTypeName = txt.getChestTypeName(ChestType::Shop);
        break;
    default:
        break;
    }

    if (currentCount >= maxCount) {
        errorMessage = txt.getMessage(
            "chest.limit_reached",
            {
                {"type", chestTypeName           },
                {"max",  std::to_string(maxCount)}
        }
        );
        return false;
    }

    return true;
}

bool ChestService::addSharedPlayer(
    const std::string& ownerUuid,
    const std::string& targetUuid,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region
) {
    BlockPos        mainPos = getMainChestPos(pos, region);
    SharedChestData data;
    data.playerUuid = targetUuid;
    data.ownerUuid  = ownerUuid;
    data.dimId      = dimId;
    data.pos        = mainPos;
    return ChestRepository::getInstance().addSharedPlayer(data);
}

bool ChestService::removeSharedPlayer(const std::string& targetUuid, BlockPos pos, int dimId, BlockSource& region) {
    BlockPos mainPos = getMainChestPos(pos, region);
    return ChestRepository::getInstance().removeSharedPlayer(targetUuid, mainPos, dimId);
}

std::vector<std::string> ChestService::getSharedPlayers(BlockPos pos, int dimId, BlockSource& region) {
    BlockPos                 mainPos = getMainChestPos(pos, region);
    auto                     shared  = ChestRepository::getInstance().getSharedPlayers(mainPos, dimId);
    std::vector<std::string> result;
    for (const auto& s : shared) {
        result.push_back(s.playerUuid);
    }
    return result;
}

bool ChestService::updateChestConfig(BlockPos pos, int dimId, BlockSource& region, const ChestConfigData& config) {
    BlockPos mainPos = getMainChestPos(pos, region);
    bool     success = ChestRepository::getInstance()
                       .updateConfig(mainPos, dimId, config.enableFloatingText, config.enableFakeItem, config.isPublic);

    if (success) {
        auto& ftm = FloatingTextManager::getInstance();
        if (!config.enableFloatingText) {
            ftm.removeFloatingText(mainPos, dimId);
        } else {
            auto info = getChestInfo(pos, dimId, region);
            if (info) {
                updateFloatingText(mainPos, dimId, info->ownerUuid, info->type);
                if (info->type == ChestType::Shop || info->type == ChestType::RecycleShop) {
                    ftm.updateShopFloatingText(mainPos, dimId, info->type);
                }
            }
        }
    }
    return success;
}

ChestConfigData ChestService::getChestConfig(BlockPos pos, int dimId, BlockSource& region) {
    BlockPos mainPos = getMainChestPos(pos, region);
    auto     data    = ChestRepository::getInstance().findByPosition(mainPos, dimId);

    ChestConfigData config;
    if (data) {
        config.enableFloatingText = data->enableFloatingText;
        config.enableFakeItem     = data->enableFakeItem;
        config.isPublic           = data->isPublic;
    }
    return config;
}

bool ChestService::setShopName(BlockPos pos, int dimId, BlockSource& region, const std::string& name) {
    BlockPos mainPos = getMainChestPos(pos, region);
    return ChestRepository::getInstance().updateShopName(mainPos, dimId, name);
}

std::string ChestService::getShopName(BlockPos pos, int dimId, BlockSource& region) {
    BlockPos mainPos = getMainChestPos(pos, region);
    auto     data    = ChestRepository::getInstance().findByPosition(mainPos, dimId);
    return data ? data->shopName : "";
}

int ChestService::getPlayerChestCount(const std::string& playerUuid, ChestType type) {
    return ChestRepository::getInstance().countByOwnerAndType(playerUuid, type);
}

std::vector<ChestData> ChestService::getAllPublicChests() {
    auto                   allChests = ChestRepository::getInstance().findAll();
    std::vector<ChestData> publicChests;
    for (const auto& chest : allChests) {
        if (chest.isPublic && (chest.type == ChestType::Shop || chest.type == ChestType::RecycleShop)) {
            publicChests.push_back(chest);
        }
    }
    return publicChests;
}

std::string ChestService::generateFloatingText(ChestType type, const std::string& ownerName) {
    return TextService::getInstance().generateChestText(type, ownerName);
}

void ChestService::updateFloatingText(BlockPos pos, int dimId, const std::string& ownerUuid, ChestType type) {
    std::string ownerName = ownerUuid;
    if (auto info = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(ownerUuid))) {
        ownerName = info->name;
    }
    std::string text = generateFloatingText(type, ownerName);
    FloatingTextManager::getInstance().addOrUpdateFloatingText(pos, dimId, ownerUuid, text, type);
}

} // namespace CT