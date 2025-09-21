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

#include "ll\api\event\player\PlayerDestroyBlockEvent.h"
#include "mc/world/actor/Hopper.h"
#include "mc/world/level/block/HopperBlock.h"
#include "mc/world/level/block/actor/PistonBlockActor.h"
#include "mc\world\actor\boss\WitherBoss.h"
#include "mc\world\actor\monster\EnderDragon.h"

#include "mc\world\actor\provider\SynchedActorDataAccess.h"
namespace CT {


void registerEventListener() {
    auto& eventBus = ll::event::EventBus::getInstance();

    eventBus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](ll::event::PlayerInteractBlockEvent& ev) {
        auto&       player         = ev.self();
        auto&       item           = player.getCarriedItem();
        auto        block          = ev.block();
        auto        dimId          = player.getDimensionId();
        auto        pos            = ev.blockPos();
        bool        isHoldingStick = (item.getTypeName() == "minecraft:stick");
        std::string player_uuid    = player.getUuid().asString();
        auto&       region         = player.getDimensionBlockSource();
        if (block->getTypeName() != "minecraft:chest") {
            return; // 只处理箱子
        }
        if (!SynchedActorDataAccess::getActorFlag(player.getEntityContext(), ActorFlags::Sneaking)) {
            return; // 只处理潜行状态下的交互
        }

        auto [isLocked, ownerUuid, chestType] = getChestDetails(pos, static_cast<int>(dimId), region);

        if (isLocked) {
            // 箱子已被锁定
            bool isOwner = (ownerUuid == player_uuid);

            if (isOwner) {
                // 玩家是箱子主人
                if (isHoldingStick) {
                    showChestLockForm(player, pos, static_cast<int>(dimId), isLocked, ownerUuid, chestType, region);
                    ev.cancel();
                    return;
                }
                // 主人没有手持木棍，根据箱子类型决定行为
                // （当前版本，主人可以打开所有类型的箱子）

            } else {
                // 玩家不是箱子主人，检查是否是分享玩家
                std::vector<std::string> sharedPlayers  = getSharedPlayers(pos, static_cast<int>(dimId));
                bool                     isSharedPlayer = false;
                for (const std::string& sharedPlayerUuid : sharedPlayers) {
                    if (sharedPlayerUuid == player_uuid) {
                        isSharedPlayer = true;
                        break;
                    }
                }

                if (isSharedPlayer) {
                    // 玩家是分享玩家，允许打开箱子，但不能通过木棍操作表单
                    if (isHoldingStick) {
                        ev.cancel(); // 阻止打开表单
                        player.sendMessage("§e你已被分享此箱子，但只有主人才能使用木棍管理箱子。");
                        logger.info(
                            "玩家 {} (分享玩家) 手持木棍尝试操作已锁定箱子 ({}, {}, {}) in dim {}，已阻止表单显示。",
                            player_uuid,
                            pos.x,
                            pos.y,
                            pos.z,
                            static_cast<int>(dimId)
                        );
                        return;
                    }
                    // 分享玩家没有手持木棍，允许打开箱子
                } else {
                    // 玩家既不是箱子主人也不是分享玩家
                    if (chestType == ChestType::Shop) {
                        // 如果是商店箱子，显示物品详情
                        showShopChestItemsForm(player, pos, static_cast<int>(dimId), region);
                        ev.cancel();
                        logger.info(
                            "玩家 {} 尝试打开商店箱子 ({}, {}, {}) in dim {}，已显示物品详情。",
                            player_uuid,
                            pos.x,
                            pos.y,
                            pos.z,
                            static_cast<int>(dimId)
                        );
                        return;
                    } else if (chestType == ChestType::Public) {
                        // 如果是公共箱子，允许任何玩家打开
                        logger.info(
                            "玩家 {} 尝试打开公共箱子 ({}, {}, {}) in dim {}，已允许。",
                            player_uuid,
                            pos.x,
                            pos.y,
                            pos.z,
                            static_cast<int>(dimId)
                        );
                        // 不取消事件，允许打开
                    } else {
                        // 其他类型的锁定箱子，阻止打开
                        ev.cancel();
                        player.sendMessage("§c这个箱子已经被锁定了，你不是它的主人，也没有被分享！");
                        logger.info(
                            "玩家 {} 尝试打开被玩家 {} 锁定的箱子 ({}, {}, {}) in dim {}，已阻止。",
                            player_uuid,
                            ownerUuid,
                            pos.x,
                            pos.y,
                            pos.z,
                            static_cast<int>(dimId)
                        );
                        return;
                    }
                }
            }
        } else {
            // 箱子未被锁定
            if (isHoldingStick) {
                showChestLockForm(player, pos, static_cast<int>(dimId), isLocked, ownerUuid, chestType, region);
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
    });
    ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerDestroyBlockEvent>(
        [](ll::event::PlayerDestroyBlockEvent& event) {
            auto& player = event.self();
            auto  dimId  = static_cast<int>(player.getDimensionId());
            auto  pos    = event.pos();
            auto& region = player.getDimensionBlockSource();
            auto& block  = region.getBlock(pos);

            if (block.getTypeName() == "minecraft:chest") {
                auto [locked, ownerUuid, chestType] = CT::getChestDetails(pos, dimId, region);
                if (locked) {
                    event.cancel();
                    player.sendMessage("§c这个上锁的箱子不能被破坏！");
                    logger.info(
                        "玩家 {} 尝试破坏被玩家 {} 锁定的箱子 ({}, {}, {}) in dim {}，已阻止。",
                        player.getUuid().asString(),
                        ownerUuid,
                        pos.x,
                        pos.y,
                        pos.z,
                        dimId
                    );
                }
            }
        }
    );
}


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
                        auto [locked, ownerUuid, chestType] = CT::getChestDetails(currentPos, dimId, region);
                        if (locked) {
                            logger.info(
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

    logger.info(
        "hook3: _tryToPairWith called for currentChest ({}, {}, {}) and otherChest ({}, {}, {}) in dim {}",
        currentChestPos->x,
        currentChestPos->y,
        currentChestPos->z,
        otherChestPos.x,
        otherChestPos.y,
        otherChestPos.z,
        dim
    );

    // 检查当前箱子是否被锁定
    auto [currentChestLocked, currentChestOwnerUuid, currentChestType] = getChestDetails(currentChestPos, dim, region);
    // 检查尝试配对的另一个箱子是否被锁定
    auto [otherChestLocked, otherChestOwnerUuid, otherChestType] = getChestDetails(otherChestPos, dim, region);

    logger.info("hook3: currentChestLocked: {}, otherChestLocked: {}", currentChestLocked, otherChestLocked);

    if (currentChestLocked || otherChestLocked) {
        // 如果当前箱子或尝试配对的箱子中任何一个被锁定，则禁止其变成大箱子，直接返回
        logger.info(
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

            if (actorContextResult->isValid()) {
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
                        auto [locked, ownerUuid, chestType] = getChestDetails(*pos, dim, region);
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
                        auto [locked, ownerUuid, chestType] = CT::getChestDetails(currentPos, dimId, region);
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
) {
    // 检查被推动的方块是否存在且是箱子 (curPos即为被推动方块的坐标)
    if (region.hasBlock(curPos)) {
        auto& block = region.getBlock(curPos);
        if (block.getTypeName() == "minecraft:chest") {
            int dimId                           = static_cast<int>(region.getDimensionId());
            auto [locked, ownerUuid, chestType] = CT::getChestDetails(curPos, dimId, region);
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


} // namespace CT
