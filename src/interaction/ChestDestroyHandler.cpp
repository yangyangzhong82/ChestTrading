#include "interaction/ChestDestroyHandler.h"

#include "Bedrock-Authority/permission/PermissionManager.h"
#include "logger.h"
#include "mc/world/level/block/Block.h"
#include "service/ChestService.h"

namespace CT {

void handlePlayerDestroyBlock(ll::event::PlayerDestroyBlockEvent& event) {
    auto& player = event.self();
    auto  dimId  = static_cast<int>(player.getDimensionId());
    auto  pos    = event.pos();
    auto& region = player.getDimensionBlockSource();
    auto& block  = region.getBlock(pos);

    if (block.getTypeName() != "minecraft:chest") {
        return;
    }

    auto& chestService = ChestService::getInstance();
    auto  chestInfo    = chestService.getChestInfo(pos, dimId, region);
    if (!chestInfo) {
        return;
    }

    bool isAdmin = BA::permission::PermissionManager::getInstance().hasPermission(player.getUuid().asString(),
                                                                                "chest.admin");
    if (isAdmin) {
        chestService.removeChest(pos, dimId, region);
        player.sendMessage("§a管理员权限：已移除箱子锁定数据。");
        return;
    }

    event.cancel();
    player.sendMessage("§c这个上锁的箱子不能被破坏！");

    logger.debug(
        "玩家 {} 尝试破坏被玩家 {} 锁定的箱子 ({}, {}, {}) in dim {}，已阻止。",
        player.getUuid().asString(),
        chestInfo->ownerUuid,
        pos.x,
        pos.y,
        pos.z,
        dimId
    );
}

} // namespace CT
