#include "event.h"
#include "db/Sqlite3Wrapper.h" // 引入 Sqlite3Wrapper
#include "form/LockForm.h"
#include "interaction/chestprotect.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "ll/api/memory/Hook.h"
#include "logger.h"
#include "mc/platform/UUID.h" // 引入 UUID
#include "mc/world/events/ExplosionStartedEvent.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Explosion.h"
#include "mc/world/level/block/Block.h"
#include "mc\world\level\block\actor\ChestBlockActor.h"

#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/game_refs/WeakRef.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/events/ActorEventCoordinator.h"
#include "mc/world/events/ActorGameplayEvent.h"
#include "mc/world/events/ActorGriefingBlockEvent.h"

#include "mc/world/actor/Hopper.h"
#include "mc/world/level/block/HopperBlock.h"
#include "mc/world/level/block/actor/PistonBlockActor.h"
#include "mc\world\actor\boss\WitherBoss.h"

namespace CT {



void registerEventListener() {
    auto& eventBus = ll::event::EventBus::getInstance();

    eventBus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
        [](ll::event::PlayerInteractBlockEvent& ev) {
            auto& player            = ev.self();
            auto& item              = player.getCarriedItem();
            auto  block             = ev.block();
            auto  dimId             = player.getDimensionId();
            auto  pos               = ev.blockPos();
            bool  isHoldingStick    = (item.getTypeName() == "minecraft:stick");
            std::string player_uuid = player.getUuid().asString();
            auto& region = player.getDimensionBlockSource();
            if (block->getTypeName() != "minecraft:chest") {
                return; // 只处理箱子
            }
            auto [currentChestLocked, currentChestOwnerUuid] = isChestLocked(pos, static_cast<int>(dimId));
            auto* blockActor = region.getBlockEntity(pos);
            auto chest = static_cast<class ChestBlockActor*>(blockActor);
             auto pairedChestBlockActor =
                static_cast<class ChestBlockActor*>(blockActor)->mLargeChestPaired;

            bool finalLocked = currentChestLocked;
            std::string finalOwnerUuid = currentChestOwnerUuid;
            std::string pairedChestOwnerUuid = ""; // 初始化配对箱子主人UUID

            // 如果是双箱子，并且另一个箱子存在
            if (pairedChestBlockActor) {
                auto pairedChestPos = chest->mLargeChestPairedPosition; // 另一个箱子坐标
                auto [isPairedChestLocked, tempPairedChestOwnerUuid] = isChestLocked(pairedChestPos, static_cast<int>(dimId));
                pairedChestOwnerUuid = tempPairedChestOwnerUuid; // 存储配对箱子主人UUID

                // 只要其中一个箱子被锁定，整个大箱子就被视为锁定
                if (isPairedChestLocked) {
                    finalLocked = true;
                    // 如果当前箱子未锁定，则以配对箱子的主人为准
                    if (!currentChestLocked) {
                        finalOwnerUuid = pairedChestOwnerUuid;
                    }
                    // 如果两个都锁定，finalOwnerUuid 保持 currentChestOwnerUuid，
                    // 但在后续权限检查时需要同时考虑两个主人
                }
            }
            
            if (finalLocked) {
                // 箱子已被锁定
                bool isOwner = (finalOwnerUuid == player_uuid);
                if (pairedChestBlockActor && !isOwner && !pairedChestOwnerUuid.empty()) {
                    // 如果是双箱子，且当前玩家不是第一个箱子的主人，检查是否是第二个箱子的主人
                    isOwner = (pairedChestOwnerUuid == player_uuid);
                }

                if (!isOwner) {
                    // 玩家不是箱子主人
                    ev.cancel();
                    player.sendMessage("§c这个箱子已经被锁定了，你不是它的主人！");
                    logger.info(
                        "玩家 {} 尝试打开被玩家 {} 锁定的箱子 ({}, {}, {}) in dim {}",
                        player_uuid,
                        finalOwnerUuid,
                        pos.x,
                        pos.y,
                        pos.z,
                        static_cast<int>(dimId)
                    );
                    return;
                }
                // 玩家是箱子主人
                if (isHoldingStick) {
                    showChestLockForm(player, pos, static_cast<int>(dimId), finalLocked, finalOwnerUuid, region);
                    ev.cancel();
                    logger.info(
                        "玩家 {} (主人) 手持木棍尝试操作已锁定箱子 ({}, {}, {}) in dim {}",
                        player_uuid,
                        pos.x,
                        pos.y,
                        pos.z,
                        static_cast<int>(dimId)
                    );
                    return;
                }
                // 主人没有手持木棍，允许打开箱子
            }
            else {
                // 箱子未被锁定
                if (isHoldingStick) {
                    showChestLockForm(player, pos, static_cast<int>(dimId), finalLocked, finalOwnerUuid, region);
                    ev.cancel();
                    logger.info(
                        "玩家 {} 手持木棍尝试操作未锁定箱子 ({}, {}, {}) in dim {}",
                        player_uuid,
                        pos.x,
                        pos.y,
                        pos.z,
                        static_cast<int>(dimId)
                    );
                    return;
                }
                // 未锁定且没有手持木棍，允许打开箱子
            }
        }
    );
}




LL_AUTO_TYPE_INSTANCE_HOOK(
    hook3,
    ll::memory::HookPriority::Normal,
    ChestBlockActor,
    &ChestBlockActor::_tryToPairWith,
    void,
     ::BlockSource& region,
    ::BlockPos const&   position
) {
   auto currentChestPos = this->mPosition;
   auto otherChestPos = position; // 尝试配对的另一个箱子的位置
   auto dim = static_cast<int>(region.getDimensionId());

   logger.info(
       "hook3: _tryToPairWith called for currentChest ({}, {}, {}) and otherChest ({}, {}, {}) in dim {}",
       currentChestPos->x, currentChestPos->y, currentChestPos->z,
       otherChestPos.x, otherChestPos.y, otherChestPos.z,
       dim
   );

   // 检查当前箱子是否被锁定
   auto [currentChestLocked, currentChestOwnerUuid] = isChestLocked(currentChestPos, dim);
   // 检查尝试配对的另一个箱子是否被锁定
   auto [otherChestLocked, otherChestOwnerUuid] = isChestLocked(otherChestPos, dim);

   logger.info(
       "hook3: currentChestLocked: {}, otherChestLocked: {}",
       currentChestLocked,
       otherChestLocked
   );

   if (currentChestLocked || otherChestLocked) {
       // 如果当前箱子或尝试配对的箱子中任何一个被锁定，则禁止其变成大箱子，直接返回
       logger.info(
           "尝试将锁定的箱子 ({}, {}, {}) 或 ({}, {}, {}) in dim {} 变成大箱子被阻止。",
           currentChestPos->x, currentChestPos->y, currentChestPos->z,
           otherChestPos.x, otherChestPos.y, otherChestPos.z,
           dim
       );
       return;
   }

   // 如果两个箱子都未被锁定，则执行原始逻辑
   origin(region,position);
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

            if (actorContextResult->isValid()) {
                // 获取 EntityContext 引用
                ::EntityContext& entityContext = actorContextResult.value();
                // 使用 Actor::tryGetFromEntity 获取 Actor 指针
                ::Actor* actor = Actor::tryGetFromEntity(entityContext, false);

                if (actor) {

                    auto dim                           = static_cast<int>(actor->getDimensionId());
                    auto pos = griefingEvent->mPos;
                    auto& region = actor->getDimensionBlockSource(); // 获取 BlockSource

                    // 获取方块
                    auto block = griefingEvent->mBlock;
                    if (block->getTypeName() == "minecraft:chest") {
                        // 检查是否是上锁的箱子
                    auto [locked, ownerUuid] = isChestLocked(*pos, dim);
                        if (locked) {
                            logger.info(
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
    }  catch (...) {
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
    logger.info("触发WitherBoss::_destroyBlocks hook");
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
                        auto [locked, ownerUuid] = CT::isChestLocked(currentPos, dimId);
                        if (locked) {
                            logger.info(
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
){
    // 检查被推动的方块是否存在且是箱子 (curPos即为被推动方块的坐标)
    if (region.hasBlock(curPos)) {
        auto& block = region.getBlock(curPos);
        if (block.getTypeName() == "minecraft:chest") {
            int dimId = static_cast<int>(region.getDimensionId());
            auto [locked, ownerUuid] = CT::isChestLocked(curPos, dimId);
            if (locked) {
                logger.info(
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
    if(this->mIsEntity){
        return origin(region, toContainer, pos);
    }
    BlockPos chestPos(static_cast<int>(pos.x), static_cast<int>(pos.y) + 1, static_cast<int>(pos.z)); // 漏斗从上方吸取物品，所以目标箱子在漏斗上方
    int dimId = static_cast<int>(region.getDimensionId());

    auto [locked, ownerUuid] = CT::isChestLocked(chestPos, dimId);
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
    logger.info("朝向:{}",attachedFace);
    BlockPos chestPos = BlockPos(static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z));
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

    auto [locked, ownerUuid] = CT::isChestLocked(chestPos, dimId);
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
}
