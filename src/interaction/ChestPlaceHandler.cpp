#include "interaction/ChestPlaceHandler.h"

#include "FloatingText/FloatingText.h"
#include "Utils/ChestTypeUtils.h"
#include "Utils/NbtUtils.h"
#include "compat/PermissionCompat.h"
#include "compat/PLandCompat.h"
#include "logger.h"
#include "mc/nbt/CompoundTag.h"
#include "mc/nbt/CompoundTagVariant.h"
#include "repository/ChestRepository.h"
#include "service/ChestService.h"
#include "service/TextService.h"


#include <mutex>
#include <unordered_map>

namespace CT {

namespace {
// 缓存放置前的 packed_id（玩家UUID -> packed_id）
std::unordered_map<std::string, int64_t> gPendingPackedId;
std::mutex                               gPendingMutex;
} // namespace

void handlePlayerPlacingBlock(ll::event::PlayerPlacingBlockEvent& ev) {
    if (ev.isCancelled()) {
        return;
    }

    auto& player = ev.self();
    auto& item   = player.getCarriedItem();

    logger.debug("handlePlayerPlacingBlock: item={}", item.getTypeName());

    if (!ChestTypeUtils::isSupportedChestItemTypeName(item.getTypeName())) {
        return;
    }

    if (!PLandCompat::getInstance().canPlace(player, ev.pos())) {
        ev.cancel();
        player.sendMessage(TextService::getInstance().getMessage("chest.land_no_permission_place"));
        return;
    }

    // 获取物品NBT
    auto itemNbt = NbtUtils::getItemNbt(item);
    if (!itemNbt) {
        logger.debug("handlePlayerPlacingBlock: no itemNbt");
        return;
    }

    if (!itemNbt->contains("tag")) {
        logger.debug("handlePlayerPlacingBlock: no tag in itemNbt");
        return;
    }

    auto& tagNbt = itemNbt->at("tag").get<CompoundTag>();
    if (!tagNbt.contains("ChestTrading")) {
        logger.debug("handlePlayerPlacingBlock: no ChestTrading in tag");
        return;
    }

    auto& ctData = tagNbt.at("ChestTrading").get<CompoundTag>();
    if (!ctData.contains("packedId")) {
        logger.debug("handlePlayerPlacingBlock: no packedId in ChestTrading");
        return;
    }

    int64_t packedId = ctData.at("packedId").get<Int64Tag>();
    logger.info("handlePlayerPlacingBlock: found packedId={}", packedId);

    std::string playerUuid = player.getUuid().asString();
    if (!PermissionCompat::hasPermission(playerUuid, "chest.pack")) {
        ev.cancel();
        player.sendMessage(TextService::getInstance().getMessage("command.no_permission"));
        return;
    }

    // 缓存 packed_id 供放置后使用
    {
        std::lock_guard<std::mutex> lock(gPendingMutex);
        gPendingPackedId[playerUuid] = packedId;
    }
}

void handlePlayerPlacedBlock(ll::event::PlayerPlacedBlockEvent& ev) {
    auto& placedBlock = ev.placedBlock();
    logger.debug("handlePlayerPlacedBlock: block={}", placedBlock.getTypeName());

    if (!ChestTypeUtils::isSupportedChestTypeName(placedBlock.getTypeName())) {
        return;
    }

    auto&       player     = ev.self();
    std::string playerUuid = player.getUuid().asString();
    auto        pos        = ev.pos();
    int         dimId      = static_cast<int>(player.getDimensionId());

    // 从缓存获取 packed_id
    int64_t packedId = -1;
    {
        std::lock_guard<std::mutex> lock(gPendingMutex);
        auto                        it = gPendingPackedId.find(playerUuid);
        if (it == gPendingPackedId.end()) {
            logger.debug("handlePlayerPlacedBlock: no cached packedId for player {}", playerUuid);
            return;
        }
        packedId = it->second;
        gPendingPackedId.erase(it);
    }

    logger.info(
        "handlePlayerPlacedBlock: unpacking chest packedId={} to ({}, {}, {}) dim {}",
        packedId,
        pos.x,
        pos.y,
        pos.z,
        dimId
    );

    // 恢复箱子配置
    if (ChestRepository::getInstance().unpackChest(packedId, pos, dimId)) {
        logger.info("Restored packed chest {} at ({}, {}, {}) dim {}", packedId, pos.x, pos.y, pos.z, dimId);

        // 使缓存失效，确保下次查询从数据库读取最新数据
        ChestCacheManager::getInstance().invalidateCache(pos, dimId);

        // 更新悬浮字
        auto chestInfo = ChestRepository::getInstance().findByPosition(pos, dimId);
        if (chestInfo) {
            FloatingTextManager::getInstance().addOrUpdateFloatingText(
                pos,
                dimId,
                chestInfo->ownerUuid,
                TextService::getInstance().generateChestText(chestInfo->type, chestInfo->ownerUuid),
                chestInfo->type
            );
            if (chestInfo->type == ChestType::Shop || chestInfo->type == ChestType::RecycleShop) {
                FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, chestInfo->type);
            }
        }

        player.sendMessage("§a已恢复打包箱子的配置！");
    } else {
        logger.error("Failed to restore packed chest {} at ({}, {}, {}) dim {}", packedId, pos.x, pos.y, pos.z, dimId);
        player.sendMessage("§c恢复箱子配置失败！");
    }
}

} // namespace CT
