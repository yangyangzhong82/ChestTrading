#include "interaction/ChestInteractHandler.h"

#include "Bedrock-Authority/permission/PermissionManager.h"
#include "Config/ConfigManager.h"
#include "Utils/NbtUtils.h"
#include "command/command.h"
#include "form/LockForm.h"
#include "form/RecycleForm.h"
#include "form/ShopForm.h"
#include "logger.h"
#include "mc/nbt/CompoundTagVariant.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "service/ChestService.h"
#include "service/TextService.h"

#include <chrono>
#include <map>
#include <mutex>
#include <string>

namespace CT {
namespace {

// 每个玩家的上次交互时间（用于箱子交互防抖）
std::map<std::string, std::chrono::steady_clock::time_point> gLastInteractionTime;
std::mutex                                                   gLastInteractionTimeMutex;

bool shouldDebounce(const std::string& playerUuid) {
    auto now = std::chrono::steady_clock::now();

    auto& config           = CT::ConfigManager::getInstance().get();
    auto  debounceInterval = std::chrono::milliseconds(config.interactionSettings.debounceIntervalMs);
    auto  cleanupThreshold = std::chrono::seconds(config.interactionSettings.cleanupThresholdSec);

    std::lock_guard<std::mutex> lock(gLastInteractionTimeMutex);

    for (auto it = gLastInteractionTime.begin(); it != gLastInteractionTime.end();) {
        if (now - it->second > cleanupThreshold) {
            it = gLastInteractionTime.erase(it);
        } else {
            ++it;
        }
    }

    auto found = gLastInteractionTime.find(playerUuid);
    if (found != gLastInteractionTime.end()) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - found->second) < debounceInterval) {
            return true;
        }
    }

    gLastInteractionTime[playerUuid] = now;
    return false;
}

bool tryHandlePackChestMode(Player& player, BlockPos originalPos, int dimId, BlockSource& region) {
    std::string playerUuid = player.getUuid().asString();

    if (!isInPackChestMode(playerUuid)) {
        return false;
    }

    setPackChestMode(playerUuid, false);

    auto& chestService = ChestService::getInstance();
    auto  mainPos      = chestService.getMainChestPos(originalPos, region);

    // 若是特殊箱子（有配置），仅允许主人打包；且需先清理配置记录
    auto chestInfo = chestService.getChestInfo(mainPos, dimId, region);
    if (chestInfo.has_value()) {
        if (chestInfo->ownerUuid != playerUuid) {
            player.sendMessage("§c你不是这个箱子的主人，无法打包。");
            return true;
        }
        chestService.removeChest(mainPos, dimId, region);
    }

    auto* blockActor = region.getBlockEntity(originalPos);
    if (!blockActor) {
        player.sendMessage("§c无法获取箱子数据。");
        return true;
    }

    auto chestNbt = NbtUtils::getBlockEntityNbt(blockActor);
    if (!chestNbt || !chestNbt->contains("Items")) {
        player.sendMessage("§c箱子为空或无法读取。");
        return true;
    }

    CompoundTag itemNbt;
    itemNbt["Name"]        = StringTag("minecraft:chest");
    itemNbt["Count"]       = ByteTag(1);
    itemNbt["Damage"]      = ShortTag(0);
    itemNbt["WasPickedUp"] = ByteTag(0);

    CompoundTag tagNbt;
    tagNbt["Items"] = chestNbt->at("Items").get<ListTag>();
    itemNbt["tag"]  = std::move(tagNbt);

    auto chestItem = NbtUtils::createItemFromNbt(itemNbt);
    if (!chestItem) {
        player.sendMessage("§c创建物品失败。");
        return true;
    }

    if (!player.addAndRefresh(*chestItem)) {
        player.sendMessage("§c背包已满，无法打包箱子。");
        return true;
    }

    auto* chestActor = static_cast<ChestBlockActor*>(blockActor);
    chestActor->clearInventory(0);

    region.removeBlock(originalPos, BlockChangeContext{});

    player.sendMessage("§a箱子已打包成物品！");
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

    bool isAdmin = BA::permission::PermissionManager::getInstance().hasPermission(playerUuid, "chest.admin");
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
        if (chestType == ChestType::Shop || chestType == ChestType::RecycleShop) {
            if (isOwner) {
                return false;
            }
            if (chestType == ChestType::Shop) {
                showShopChestItemsForm(player, pos, dimId, region);
            } else {
                showRecycleForm(player, pos, dimId, region);
            }
            return true;
        }
        return false;
    }

    if (chestType == ChestType::Shop) {
        showShopChestItemsForm(player, pos, dimId, region);
        return true;
    }

    if (chestType == ChestType::RecycleShop) {
        showRecycleForm(player, pos, dimId, region);
        return true;
    }

    player.sendMessage(TextService::getInstance().getMessage("chest.locked"));
    return true;
}

} // namespace

void handlePlayerInteractBlock(ll::event::PlayerInteractBlockEvent& ev) {
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

    bool isAdmin = BA::permission::PermissionManager::getInstance().hasPermission(playerUuid, "chest.admin");
    bool isOwner = (ownerUuid == playerUuid) || isAdmin;

    if (item.getTypeName() == "minecraft:stick") {
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
