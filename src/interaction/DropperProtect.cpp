#include "ll/api/memory/Hook.h"
#include "logger.h"

#include "Utils/ChestTypeUtils.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/Facing.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/DropperBlock.h"
#include "mc/world/level/block/actor/DropperBlockActor.h"
#include "service/ChestService.h"

#include <optional>

namespace CT {

namespace {

std::optional<int> resolveDropperFacing(Block const& block) {
    if (auto* state = block.getBlockType().getBlockState(HashedString("facing_direction"))) {
        if (auto facing = block.getState<int>(*state)) {
            return *facing;
        }
    }

    int variant = block.getBlockType().getVariant(block);
    if (variant >= 0 && variant < static_cast<int>(Facing::STEP_X().size())) {
        return variant;
    }

    return std::nullopt;
}

std::optional<BlockPos> findSingleAdjacentChest(BlockSource& region, BlockPos const& originPos) {
    std::optional<BlockPos> result;
    for (auto const& offset : Facing::DIRECTION()) {
        BlockPos candidate = originPos + offset;
        if (!region.hasChunksAt(candidate, 0, false)) {
            continue;
        }
        if (!ChestTypeUtils::isSupportedChestBlock(region.getBlock(candidate))) {
            continue;
        }
        if (result.has_value()) {
            return std::nullopt;
        }
        result = candidate;
    }
    return result;
}

std::optional<BlockPos> resolveDropperTargetPos(BlockSource& region, BlockPos const& dropperPos, Block const& block) {
    if (block.getTypeName() != "minecraft:dropper") {
        return std::nullopt;
    }

    auto facing = resolveDropperFacing(block);
    if (facing.has_value()) {
        BlockPos targetPos = dropperPos;
        targetPos.x += Facing::STEP_X()[*facing];
        targetPos.y += Facing::STEP_Y()[*facing];
        targetPos.z += Facing::STEP_Z()[*facing];
        if (region.hasChunksAt(targetPos, 0, false) && ChestTypeUtils::isSupportedChestBlock(region.getBlock(targetPos))) {
            return targetPos;
        }
    }

    auto fallback = findSingleAdjacentChest(region, dropperPos);
    if (!fallback.has_value()) {
        logger.debug(
            "DropperProtect: 无法唯一确定投掷器 ({}, {}, {}) 的目标箱子，跳过保护检查。",
            dropperPos.x,
            dropperPos.y,
            dropperPos.z
        );
    }
    return fallback;
}

bool shouldBlockDropperInsert(BlockSource& region, BlockPos const& dropperPos) {
    if (!region.hasChunksAt(dropperPos, 0, false)) {
        return false;
    }

    auto const& block     = region.getBlock(dropperPos);
    auto        targetPos = resolveDropperTargetPos(region, dropperPos, block);
    if (!targetPos.has_value()) {
        return false;
    }

    int dimId = static_cast<int>(region.getDimensionId());
    if (!ChestService::getInstance().shouldBlockAutomatedTransfer(*targetPos, dimId, region)) {
        return false;
    }

    logger.debug(
        "投掷器尝试向受保护的箱子 ({}, {}, {}) in dim {} 塞入物品，已阻止。",
        targetPos->x,
        targetPos->y,
        targetPos->z,
        dimId
    );
    return true;
}

} // namespace

LL_AUTO_TYPE_INSTANCE_HOOK(
    DropperDispenseFromHook,
    HookPriority::Normal,
    DropperBlock,
    &DropperBlock::$dispenseFrom,
    void,
    BlockSource&    region,
    BlockPos const& pos
) {
    if (shouldBlockDropperInsert(region, pos)) {
        return;
    }
    origin(region, pos);
}

LL_AUTO_TYPE_INSTANCE_HOOK(
    DropperPushOutItemsHook,
    HookPriority::Normal,
    DropperBlockActor,
    &DropperBlockActor::pushOutItems,
    bool,
    BlockSource& region
) {
    BlockPos dropperPos = this->mPosition;
    return shouldBlockDropperInsert(region, dropperPos) ? false : origin(region);
}

} // namespace CT
