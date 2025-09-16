
#include "interaction/chestprotect.h"
#include "ll/api/memory/Hook.h"
#include "logger.h"
#include "mc/world/actor/Hopper.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/HopperBlock.h"

namespace CT {
LL_AUTO_TYPE_INSTANCE_HOOK(
    HopperPullInHook,
    HookPriority::Normal,
    Hopper,
    &Hopper::_tryPullInItemsFromAboveContainer,
    bool,
    BlockSource& region,
    Container&   toContainer,
    Vec3 const&  pos
) {
    if (this->mIsEntity) {
        return origin(region, toContainer, pos);
    }
    BlockPos chestPos(
        static_cast<int>(pos.x),
        static_cast<int>(pos.y) + 1,
        static_cast<int>(pos.z)
    ); // 漏斗从上方吸取物品，所以目标箱子在漏斗上方
    int dimId = static_cast<int>(region.getDimensionId());

    auto [locked, ownerUuid, chestType] = CT::getChestDetails(chestPos, dimId, region);
    if (locked) {
        logger.info(
            "漏斗尝试从上锁的箱子 ({}, {}, {}) in dim {} 吸取物品，已阻止。",
            chestPos.x,
            chestPos.y,
            chestPos.z,
            dimId
        );
        return false; // 阻止吸取
    }
    return origin(region, toContainer, pos); // 未锁定，执行原始逻辑
}

LL_AUTO_TYPE_INSTANCE_HOOK(
    HopperPushOutHook,
    HookPriority::Normal,
    Hopper,
    &Hopper::_pushOutItems,
    bool,
    BlockSource& region,
    Container&   fromContainer,
    Vec3 const&  position,
    int          attachedFace
) {
    if (this->mIsEntity) {
        return origin(region, fromContainer, position, attachedFace);
    }
    logger.info("朝向:{}", attachedFace);
    BlockPos chestPos =
        BlockPos(static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z));
    switch (attachedFace) {
    case 2: // Z-1
        chestPos.z -= 1;
        break;
    case 4: // X-1
        chestPos.x -= 1;
        break;
    case 3: // Z+1
        chestPos.z += 1;
        break;
    case 5: // X+1
        chestPos.x += 1;
        break;
    case 0: // 垂直，向下
        chestPos.y -= 1;
        break;
    default:
        // 对于其他朝向（例如向上），我们假设漏斗不会向这些方向推送物品到箱子，或者保持原位
        // 也可以选择在这里记录一个警告或者直接返回false
        logger.warn("HopperPushOutHook: 未知的 attachedFace 值: {}", attachedFace);
        return origin(region, fromContainer, position, attachedFace);
    }
    int dimId = static_cast<int>(region.getDimensionId());

    auto [locked, ownerUuid, chestType] = CT::getChestDetails(chestPos, dimId, region);
    if (locked) {
        logger.info(
            "漏斗尝试向上锁的箱子 ({}, {}, {}) in dim {} 推送物品，已阻止。",
            chestPos.x,
            chestPos.y,
            chestPos.z,
            dimId
        );
        return false; // 阻止推送
    }
    return origin(region, fromContainer, position, attachedFace); // 未锁定，执行原始逻辑
}
} // namespace CT
