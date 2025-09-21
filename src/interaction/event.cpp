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





} // namespace CT
