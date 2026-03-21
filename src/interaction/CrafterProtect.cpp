#include "ll/api/memory/Hook.h"
#include "logger.h"

#include "Utils/ChestTypeUtils.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/world/Facing.h"
#include "mc/world/item/ItemInstance.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/CrafterBlock.h"
#include "mc/world/level/block/actor/CrafterBlockActor.h"
#include "service/ChestService.h"

#include <array>
#include <optional>
#include <vector>

namespace CT {

namespace {

std::optional<int> resolveCrafterFacing(Block const& block) {
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

std::optional<BlockPos> resolveCrafterTargetPos(BlockSource& region, BlockPos const& crafterPos, Block const& block) {
    if (block.getTypeName() != "minecraft:crafter") {
        return std::nullopt;
    }

    auto facing = resolveCrafterFacing(block);
    if (facing.has_value()) {
        BlockPos targetPos = crafterPos;
        targetPos.x += Facing::STEP_X()[*facing];
        targetPos.y += Facing::STEP_Y()[*facing];
        targetPos.z += Facing::STEP_Z()[*facing];
        if (region.hasChunksAt(targetPos, 0, false) && ChestTypeUtils::isSupportedChestBlock(region.getBlock(targetPos))) {
            return targetPos;
        }
    }

    auto fallback = findSingleAdjacentChest(region, crafterPos);
    if (!fallback.has_value()) {
        logger.debug(
            "CrafterProtect: 无法唯一确定合成器 ({}, {}, {}) 的目标箱子，跳过保护检查。",
            crafterPos.x,
            crafterPos.y,
            crafterPos.z
        );
    }
    return fallback;
}

bool shouldBlockCrafterInsert(BlockSource& region, BlockPos const& crafterPos) {
    if (!region.hasChunksAt(crafterPos, 0, false)) {
        return false;
    }

    auto const& block     = region.getBlock(crafterPos);
    auto        targetPos = resolveCrafterTargetPos(region, crafterPos, block);
    if (!targetPos.has_value()) {
        return false;
    }

    int dimId = static_cast<int>(region.getDimensionId());
    if (!ChestService::getInstance().shouldBlockAutomatedTransfer(*targetPos, dimId, region)) {
        return false;
    }

    logger.debug(
        "合成器尝试向受保护的箱子 ({}, {}, {}) in dim {} 塞入物品，已阻止。",
        targetPos->x,
        targetPos->y,
        targetPos->z,
        dimId
    );
    return true;
}

} // namespace

LL_AUTO_TYPE_INSTANCE_HOOK(
    CrafterDispenseFromHook,
    HookPriority::Normal,
    CrafterBlock,
    &CrafterBlock::dispenseFrom,
    void,
    BlockSource&      region,
    BlockPos const&   pos
) {
    if (shouldBlockCrafterInsert(region, pos)) {
        return;
    }
    origin(region, pos);
}

LL_AUTO_TYPE_INSTANCE_HOOK(
    CrafterTryMoveItemsIntoContainerHook,
    HookPriority::Normal,
    CrafterBlockActor,
    &CrafterBlockActor::tryMoveItemsIntoContainer,
    bool,
    BlockSource&               region,
    std::vector<ItemInstance>& items
) {
    BlockPos crafterPos = this->mPosition;
    return shouldBlockCrafterInsert(region, crafterPos) ? false : origin(region, items);
}

} // namespace CT
