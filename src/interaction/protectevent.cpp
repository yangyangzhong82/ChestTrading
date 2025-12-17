#include "ll/api/memory/Hook.h"
#include "logger.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/game_refs/WeakRef.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/boss/WitherBoss.h"
#include "mc/world/actor/monster/EnderDragon.h"
#include "mc/world/events/ActorEventCoordinator.h"
#include "mc/world/events/ActorGameplayEvent.h"
#include "mc/world/events/ActorGriefingBlockEvent.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/level/block/actor/PistonBlockActor.h"
#include "service/ChestService.h"


namespace CT {


LL_AUTO_TYPE_INSTANCE_HOOK(
    hook3,
    ll::memory::HookPriority::Normal,
    EnderDragon,
    &EnderDragon::_checkWalls,
    bool,
    ::AABB bb
) {
    auto& region = this->getDimensionBlockSource();
    int   dimId  = static_cast<int>(this->getDimensionId());

    // 遍历 AABB 范围内的所有方块
    for (int x = static_cast<int>(bb.min.x); x <= static_cast<int>(bb.max.x); ++x) {
        for (int y = static_cast<int>(bb.min.y); y <= static_cast<int>(bb.max.y); ++y) {
            for (int z = static_cast<int>(bb.min.z); z <= static_cast<int>(bb.max.z); ++z) {
                BlockPos currentPos(x, y, z);
                // 检查方块是否存在且是箱子
                if (region.hasBlock(currentPos)) {
                    auto& block = region.getBlock(currentPos);
                    if (block.getTypeName() == "minecraft:chest") {
                        // 检查是否是上锁的箱子
                        if (ChestService::getInstance().isChestLocked(currentPos, dimId, region)) {
                            logger.debug(
                                "末影龙尝试破坏上锁的箱子 ({}, {}, {}) in dim {}，已阻止所有破坏。",
                                currentPos.x,
                                currentPos.y,
                                currentPos.z,
                                dimId
                            );
                            return false; // 阻止原始 _checkWalls 的执行，从而保护箱子
                        }
                    }
                }
            }
        }
    }

    // 如果没有发现上锁的箱子，则执行原始逻辑
    return origin(bb);
}
LL_AUTO_TYPE_INSTANCE_HOOK(
    hook4,
    ll::memory::HookPriority::Normal,
    ChestBlockActor,
    &ChestBlockActor::_tryToPairWith,
    void,
    ::BlockSource&    region,
    ::BlockPos const& position
) {
    auto currentChestPos = this->mPosition;
    auto otherChestPos   = position; // 尝试配对的另一个箱子的位置
    auto dim             = static_cast<int>(region.getDimensionId());

    logger.debug(
        "hook4: _tryToPairWith called for currentChest ({}, {}, {}) and otherChest ({}, {}, {}) in dim {}",
        currentChestPos->x,
        currentChestPos->y,
        currentChestPos->z,
        otherChestPos.x,
        otherChestPos.y,
        otherChestPos.z,
        dim
    );

    // 检查当前箱子是否被锁定
    auto& svc                = ChestService::getInstance();
    bool  currentChestLocked = svc.isChestLocked(currentChestPos, dim, region);
    bool  otherChestLocked   = svc.isChestLocked(otherChestPos, dim, region);

    logger.debug("hook4: currentChestLocked: {}, otherChestLocked: {}", currentChestLocked, otherChestLocked);

    if (currentChestLocked || otherChestLocked) {
        // 如果当前箱子或尝试配对的箱子中任何一个被锁定，则禁止其变成大箱子，直接返回
        logger.debug(
            "尝试将锁定的箱子 ({}, {}, {}) 或 ({}, {}, {}) in dim {} 变成大箱子被阻止。",
            currentChestPos->x,
            currentChestPos->y,
            currentChestPos->z,
            otherChestPos.x,
            otherChestPos.y,
            otherChestPos.z,
            dim
        );
        return;
    }

    // 如果两个箱子都未被锁定，则执行原始逻辑
    origin(region, position);
}

LL_AUTO_TYPE_INSTANCE_HOOK(
    ActorDestroyBlockHook,
    ll::memory::HookPriority::Normal,
    ::ActorEventCoordinator,
    &::ActorEventCoordinator::sendEvent,
    ::CoordinatorResult,
    ::EventRef<::ActorGameplayEvent<::CoordinatorResult>> const& event
) {
    try {
        const ActorGriefingBlockEvent* griefingEvent = nullptr;
        event.get().visit([&](auto&& arg) {
            using CurrentEventType = std::decay_t<decltype(arg.value())>;
            if constexpr (std::is_same_v<CurrentEventType, ActorGriefingBlockEvent>) {
                griefingEvent = &arg.value();
            }
        });

        if (griefingEvent) {
            // 获取 ActorContext 的 WeakRef
            WeakRef<::EntityContext> actorContextWeakRef = griefingEvent->mActorContext;
            // 锁定 WeakRef 获取 StackRefResult
            StackRefResult<::EntityContext> actorContextResult = actorContextWeakRef.lock();

            if (actorContextResult) {
                // 获取 EntityContext 引用
                ::EntityContext& entityContext = actorContextResult.value();
                // 使用 Actor::tryGetFromEntity 获取 Actor 指针
                ::Actor* actor = Actor::tryGetFromEntity(entityContext, false);

                if (actor) {

                    auto  dim    = static_cast<int>(actor->getDimensionId());
                    auto  pos    = griefingEvent->mPos;
                    auto& region = actor->getDimensionBlockSource(); // 获取 BlockSource

                    // 获取方块
                    auto block = griefingEvent->mBlock;
                    if (block->getTypeName() == "minecraft:chest") {
                        // 检查是否是上锁的箱子
                        if (ChestService::getInstance().isChestLocked(*pos, dim, region)) {
                            logger.debug(
                                "生物 {} 尝试破坏上锁的箱子 ({}, {}, {}) in dim {}，已阻止。",
                                actor->getTypeName(),
                                pos->x,
                                pos->y,
                                pos->z,
                                static_cast<int>(dim)
                            );
                            return CoordinatorResult::Cancel; // 阻止破坏
                        }
                    }
                    // 如果不是箱子，或者箱子未被锁定，则执行原始逻辑
                    return origin(event);
                }
            }
        }
        return origin(event);
    } catch (...) {
        logger.warn("ActorDestroyBlockHook 发生未知异常！");
        return origin(event);
    }
}
LL_AUTO_TYPE_INSTANCE_HOOK(
    ActorDestroyBlockHook2,
    ll::memory::HookPriority::Normal,
    ::WitherBoss,
    &::WitherBoss::_destroyBlocks,
    void,
    ::Level&                       level,
    ::AABB const&                  bb,
    ::BlockSource&                 region,
    int                            range,
    ::WitherBoss::WitherAttackType attackType
) {
    logger.debug("触发WitherBoss::_destroyBlocks hook");
    int dimId = static_cast<int>(region.getDimensionId());

    // 遍历 AABB 范围内的所有方块
    for (int x = static_cast<int>(bb.min.x); x <= static_cast<int>(bb.max.x); ++x) {
        for (int y = static_cast<int>(bb.min.y); y <= static_cast<int>(bb.max.y); ++y) {
            for (int z = static_cast<int>(bb.min.z); z <= static_cast<int>(bb.max.z); ++z) {
                BlockPos currentPos(x, y, z);
                // 检查方块是否存在且是箱子
                if (region.hasBlock(currentPos)) {
                    auto& block = region.getBlock(currentPos);
                    if (block.getTypeName() == "minecraft:chest") {
                        // 检查是否是上锁的箱子
                        if (ChestService::getInstance().isChestLocked(currentPos, dimId, region)) {
                            logger.debug(
                                "凋灵尝试破坏上锁的箱子 ({}, {}, {}) in dim {}，已阻止所有破坏。",
                                currentPos.x,
                                currentPos.y,
                                currentPos.z,
                                dimId
                            );
                            return; // 阻止原始 _destroyBlocks 的执行
                        }
                    }
                }
            }
        }
    }

    // 如果没有发现上锁的箱子，则执行原始逻辑
    origin(level, bb, region, range, attackType);
}

LL_AUTO_TYPE_INSTANCE_HOOK(
    PistonPushHook,
    HookPriority::Normal,
    PistonBlockActor,
    &PistonBlockActor::_attachedBlockWalker,
    bool,
    BlockSource&    region,
    BlockPos const& curPos,
    uchar           curBranchFacing,
    uchar           pistonMoveFacing
) {
    // 检查被推动的方块是否存在且是箱子 (curPos即为被推动方块的坐标)
    if (region.hasBlock(curPos)) {
        auto& block = region.getBlock(curPos);
        if (block.getTypeName() == "minecraft:chest") {
            int dimId = static_cast<int>(region.getDimensionId());
            if (ChestService::getInstance().isChestLocked(curPos, dimId, region)) {
                logger.debug(
                    "活塞尝试推动上锁的箱子 ({}, {}, {}) in dim {}，已阻止。",
                    curPos.x,
                    curPos.y,
                    curPos.z,
                    dimId
                );
                return false; // 阻止活塞推动
            }
        }
    }

    // 如果不是箱子，或者箱子未被锁定，则执行原始逻辑
    return origin(region, curPos, curBranchFacing, pistonMoveFacing);
}
} // namespace CT
