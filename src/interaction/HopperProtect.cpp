#include "Utils/ChestContainerUtils.h"
#include "Utils/ChestTypeUtils.h"
#include "compat/PLandCompat.h"
#include "ll/api/memory/Hook.h"
#include "logger.h"
#include "mc/world/actor/Hopper.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/HopperBlock.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "service/ChestService.h"

namespace CT {

namespace {

bool validateChestBlockEntity(BlockSource& region, BlockPos const& pos, char const* hookName) {
    if (!region.hasChunksAt(pos, 0, false)) {
        logger.debug(
            "{}: hopper target/source chunk is not loaded at ({}, {}, {}), skip transfer to avoid invalid access.",
            hookName,
            pos.x,
            pos.y,
            pos.z
        );
        return false;
    }

    auto const& block = region.getBlock(pos);
    if (!ChestTypeUtils::isSupportedChestBlock(block)) {
        return true;
    }

    auto* blockActor = region.getBlockEntity(pos);
    if (!blockActor) {
        logger.warn(
            "{}: chest block entity missing at ({}, {}, {}), skip hopper transfer to avoid crash.",
            hookName,
            pos.x,
            pos.y,
            pos.z
        );
        return false;
    }

    if (blockActor->mType != BlockActorType::Chest) {
        logger.warn(
            "{}: expected chest block entity at ({}, {}, {}), got type {}, skip hopper transfer.",
            hookName,
            pos.x,
            pos.y,
            pos.z,
            static_cast<int>(blockActor->mType)
        );
        return false;
    }

    if (ChestContainerUtils::tryGetChestContainer(region, pos) == nullptr) {
        logger.warn(
            "{}: chest container is null at ({}, {}, {}), skip hopper transfer to avoid crash.",
            hookName,
            pos.x,
            pos.y,
            pos.z
        );
        return false;
    }

    return true;
}

template <typename Fn>
bool callOriginWithSehGuard(Fn&& fn, char const* hookName, BlockPos const& pos, int dimId) {
#ifdef _MSC_VER
    __try {
        return fn();
    } __except (1) {
        logger.error(
            "{}: origin raised a SEH exception near ({}, {}, {}) in dim {}, transfer was cancelled.",
            hookName,
            pos.x,
            pos.y,
            pos.z,
            dimId
        );
        return false;
    }
#else
    return fn();
#endif
}

} // namespace

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
    BlockPos basePos  = BlockPos(pos);
    BlockPos chestPos = BlockPos(basePos.x, basePos.y + 1, basePos.z); // 漏斗从上方吸取物品，所以目标箱子在漏斗上方
    int      dimId    = static_cast<int>(region.getDimensionId());

    if (!validateChestBlockEntity(region, chestPos, "HopperPullInHook")) {
        return false;
    }

    if (!PLandCompat::getInstance().canHopperPullItems(chestPos, dimId)) {
        logger.debug(
            "PLand 领地禁止漏斗从箱子 ({}, {}, {}) in dim {} 吸取物品，已阻止。",
            chestPos.x,
            chestPos.y,
            chestPos.z,
            dimId
        );
        return false;
    }

    if (ChestService::getInstance().shouldBlockAutomatedPull(chestPos, dimId, region)) {
        logger.debug(
            "漏斗尝试从受保护的箱子 ({}, {}, {}) in dim {} 吸取物品，已阻止。",
            chestPos.x,
            chestPos.y,
            chestPos.z,
            dimId
        );
        return false; // 阻止吸取
    }
    return callOriginWithSehGuard(
        [&]() { return origin(region, toContainer, pos); },
        "HopperPullInHook",
        chestPos,
        dimId
    ); // 未锁定，执行原始逻辑
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
        return callOriginWithSehGuard(
            [&]() { return origin(region, fromContainer, position, attachedFace); },
            "HopperPushOutHook",
            BlockPos(position),
            static_cast<int>(region.getDimensionId())
        );
    }
    logger.trace("朝向:{}", attachedFace);
    BlockPos chestPos(position);
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
        logger.debug("HopperPushOutHook: 未知的 attachedFace 值: {}", attachedFace);
        return callOriginWithSehGuard(
            [&]() { return origin(region, fromContainer, position, attachedFace); },
            "HopperPushOutHook",
            chestPos,
            static_cast<int>(region.getDimensionId())
        );
    }
    int dimId = static_cast<int>(region.getDimensionId());

    if (!validateChestBlockEntity(region, chestPos, "HopperPushOutHook")) {
        return false;
    }

    if (ChestService::getInstance().shouldBlockAutomatedPush(chestPos, dimId, region)) {
        logger.debug(
            "漏斗尝试向受保护的箱子 ({}, {}, {}) in dim {} 推送物品，已阻止。",
            chestPos.x,
            chestPos.y,
            chestPos.z,
            dimId
        );
        return false; // 阻止推送
    }
    return callOriginWithSehGuard(
        [&]() { return origin(region, fromContainer, position, attachedFace); },
        "HopperPushOutHook",
        chestPos,
        dimId
    ); // 未锁定，执行原始逻辑
}
} // namespace CT
