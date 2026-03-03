#include "interaction/ChestInteractHandler.h"

#include "compat/PermissionCompat.h"
#include "Config/ConfigManager.h"
#include "Utils/NbtUtils.h"
#include "command/command.h"
#include "compat/PLandCompat.h"
#include "form/LockForm.h"
#include "form/RecycleForm.h"
#include "form/ShopForm.h"
#include "logger.h"
#include "mc/nbt/CompoundTagVariant.h"
#include "mc/nbt/ListTag.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "repository/ChestRepository.h"
#include "service/ChestService.h"
#include "service/TextService.h"


#include <array>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
BlockChangeContext::BlockChangeContext() : mContextSource(std::monostate{}) {}
namespace CT {
namespace {

// 分段锁优化：减少锁竞争，提升并发性能
constexpr size_t NUM_INTERACTION_SHARDS = 16; // 分段数量（2的幂次）

struct InteractionShard {
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> records;
    mutable std::mutex                                                     mutex;
    std::chrono::steady_clock::time_point                                  lastCleanupTime;

    InteractionShard() : lastCleanupTime(std::chrono::steady_clock::now()) {}
};

std::array<InteractionShard, NUM_INTERACTION_SHARDS> gInteractionShards;

// 根据 UUID 计算分段索引（使用哈希）
inline size_t getShardIndex(const std::string& playerUuid) {
    return std::hash<std::string>{}(playerUuid) & (NUM_INTERACTION_SHARDS - 1);
}

bool shouldDebounce(const std::string& playerUuid) {
    auto now = std::chrono::steady_clock::now();

    auto& config           = CT::ConfigManager::getInstance().get();
    auto  debounceInterval = std::chrono::milliseconds(config.interactionSettings.debounceIntervalMs);
    auto  cleanupThreshold = std::chrono::seconds(config.interactionSettings.cleanupThresholdSec);

    auto&                       shard = gInteractionShards[getShardIndex(playerUuid)];
    std::lock_guard<std::mutex> lock(shard.mutex); // 只锁定一个分段

    // 延迟清理：仅当距离上次清理超过阈值时才执行
    if (now - shard.lastCleanupTime > cleanupThreshold) {
        for (auto it = shard.records.begin(); it != shard.records.end();) {
            if (now - it->second > cleanupThreshold) {
                it = shard.records.erase(it);
            } else {
                ++it;
            }
        }
        shard.lastCleanupTime = now;
    }

    auto found = shard.records.find(playerUuid);
    if (found != shard.records.end()) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - found->second) < debounceInterval) {
            return true; // 触发防抖
        }
    }

    shard.records[playerUuid] = now;
    return false;
}

bool tryHandlePackChestMode(Player& player, BlockPos originalPos, int dimId, BlockSource& region) {
    std::string playerUuid = player.getUuid().asString();

    if (!isInPackChestMode(playerUuid)) {
        return false;
    }

    setPackChestMode(playerUuid, false);

    auto& txt = TextService::getInstance();

    // Packing removes the original chest block, so destroy permission is required.
    if (!PLandCompat::getInstance().canDestroy(player, originalPos)) {
        player.sendMessage(txt.getMessage("chest.land_no_permission_pack"));
        return true;
    }

    // 检查是否为大箱子，禁止打包大箱子
    auto* blockActor = region.getBlockEntity(originalPos);
    if (blockActor && blockActor->mType == BlockActorType::Chest) {
        auto* chest = static_cast<ChestBlockActor*>(blockActor);
        if (chest->mLargeChestPaired) {
            player.sendMessage(txt.getMessage("chest.pack_large_chest_forbidden"));
            return true;
        }
    }

    auto& chestService = ChestService::getInstance();
    auto  mainPos      = chestService.getMainChestPos(originalPos, region);

    // 若是特殊箱子（有配置），仅允许主人打包
    auto chestInfo = chestService.getChestInfo(mainPos, dimId, region);
    if (chestInfo.has_value()) {
        if (chestInfo->ownerUuid != playerUuid) {
            player.sendMessage(txt.getMessage("chest.pack_not_owner"));
            return true;
        }
    }

    if (!blockActor) {
        player.sendMessage(txt.getMessage("chest.pack_entity_fail"));
        return true;
    }

    auto chestNbt = NbtUtils::getBlockEntityNbt(blockActor);
    if (!chestNbt || !chestNbt->contains("Items")) {
        player.sendMessage(txt.getMessage("chest.pack_empty"));
        return true;
    }

    CompoundTag itemNbt;
    itemNbt["Name"]        = StringTag("minecraft:chest");
    itemNbt["Count"]       = ByteTag(1);
    itemNbt["Damage"]      = ShortTag(0);
    itemNbt["WasPickedUp"] = ByteTag(0);

    CompoundTag tagNbt;
    tagNbt["Items"] = chestNbt->at("Items").get<ListTag>();

    // 如果有配置，打包到数据库并只存储 packed_id
    if (chestInfo.has_value()) {
        int64_t packedId = ChestRepository::getInstance().packChest(mainPos, dimId);
        if (packedId < 0) {
            player.sendMessage(txt.getMessage("chest.pack_config_fail"));
            return true;
        }

        CompoundTag ctData;
        ctData["packedId"]     = Int64Tag(packedId);
        tagNbt["ChestTrading"] = std::move(ctData);
    }

    itemNbt["tag"] = std::move(tagNbt);

    auto chestItem = NbtUtils::createItemFromNbt(itemNbt);
    if (!chestItem) {
        player.sendMessage(txt.getMessage("chest.pack_item_fail"));
        return true;
    }

    if (!player.addAndRefresh(*chestItem)) {
        player.sendMessage(txt.getMessage("chest.pack_inventory_full"));
        return true;
    }

    auto* chestActor = static_cast<ChestBlockActor*>(blockActor);
    chestActor->clearInventory(0);

    region.removeBlock(originalPos, BlockChangeContext{});

    player.sendMessage(txt.getMessage("chest.pack_success"));
    return true;
}

void handleStickManage(
    Player&            player,
    const std::string& playerUuid,
    BlockPos           pos,
    int                dimId,
    bool               isLocked,
    const std::string& ownerUuid,
    ChestType          chestType,
    BlockSource&       region
) {
    auto& txt = TextService::getInstance();

    bool isAdmin = PermissionCompat::hasPermission(playerUuid, "chest.admin");
    bool isOwner = (ownerUuid == playerUuid) || isAdmin;

    if (isLocked && !isOwner) {
        player.sendMessage(txt.getMessage("chest.not_owner"));
        return;
    }

    showChestLockForm(player, pos, dimId, isLocked, ownerUuid, chestType, region);
}

bool handleOpenOrForms(
    Player&            player,
    const std::string& playerUuid,
    BlockPos           pos,
    int                dimId,
    ChestType          chestType,
    bool               isOwner,
    bool               isAdmin,
    BlockSource&       region
) {
    auto& chestService = ChestService::getInstance();

    if (chestService.canPlayerAccess(playerUuid, pos, dimId, region) || isAdmin) {
        if (chestType == ChestType::Shop || chestType == ChestType::RecycleShop || chestType == ChestType::AdminShop
            || chestType == ChestType::AdminRecycle) {
            if (isOwner) {
                return false;
            }
            if (chestType == ChestType::Shop || chestType == ChestType::AdminShop) {
                showShopChestItemsForm(player, pos, dimId, region);
            } else {
                showRecycleForm(player, pos, dimId, region);
            }
            return true;
        }
        return false;
    }

    if (chestType == ChestType::Shop || chestType == ChestType::AdminShop) {
        showShopChestItemsForm(player, pos, dimId, region);
        return true;
    }

    if (chestType == ChestType::RecycleShop || chestType == ChestType::AdminRecycle) {
        showRecycleForm(player, pos, dimId, region);
        return true;
    }

    player.sendMessage(TextService::getInstance().getMessage("chest.locked"));
    return true;
}

} // namespace

void handlePlayerInteractBlock(ll::event::PlayerInteractBlockEvent& ev) {
    bool wasCancelled = ev.isCancelled();

    auto block = ev.block();
    if (block->getTypeName() != "minecraft:chest") {
        return;
    }

    auto& player      = ev.self();
    auto& item        = player.getCarriedItem();
    auto  dimId       = static_cast<int>(player.getDimensionId());
    auto  originalPos = ev.blockPos();

    std::string playerUuid = player.getUuid().asString();
    auto&       region     = player.getDimensionBlockSource();

    if (shouldDebounce(playerUuid)) {
        ev.cancel();
        return;
    }

    if (tryHandlePackChestMode(player, originalPos, dimId, region)) {
        ev.cancel();
        return;
    }

    auto& chestService = ChestService::getInstance();
    auto  pos          = chestService.getMainChestPos(originalPos, region);

    auto        chestInfo = chestService.getChestInfo(pos, dimId, region);
    bool        isLocked  = chestInfo.has_value();
    std::string ownerUuid = isLocked ? chestInfo->ownerUuid : "";
    ChestType   chestType = isLocked ? chestInfo->type : ChestType::Invalid;

    bool isAdmin = PermissionCompat::hasPermission(playerUuid, "chest.admin");
    bool isOwner = (ownerUuid == playerUuid) || isAdmin;

    bool isTradeChest = chestType == ChestType::Shop || chestType == ChestType::RecycleShop
                     || chestType == ChestType::AdminShop || chestType == ChestType::AdminRecycle;
    bool allowTradeBypass = isTradeChest && !isOwner;

    // If another plugin already cancelled this interaction, only trade forms may bypass.
    if (wasCancelled && !allowTradeBypass) {
        return;
    }

    if (!PLandCompat::getInstance().canUseContainer(player, originalPos) && !allowTradeBypass) {
        player.sendMessage(TextService::getInstance().getMessage("chest.land_no_permission_container"));
        ev.cancel();
        return;
    }

    const auto& interactionSettings = ConfigManager::getInstance().get().interactionSettings;
    bool        isManageTool        = !interactionSettings.manageToolItem.empty()
                               && item.getTypeName() == interactionSettings.manageToolItem;
    bool canTriggerManage = isManageTool
                         && (!interactionSettings.requireSneakingForManage || player.isSneaking());
    if (canTriggerManage) {
        handleStickManage(player, playerUuid, pos, dimId, isLocked, ownerUuid, chestType, region);
        ev.cancel();
        return;
    }

    bool shouldCancel = handleOpenOrForms(player, playerUuid, pos, dimId, chestType, isOwner, isAdmin, region);
    if (shouldCancel) {
        ev.cancel();
        return;
    }
}

} // namespace CT

