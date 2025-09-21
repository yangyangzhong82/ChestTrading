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

        auto [isLocked, ownerUuid, chestType] = getChestDetails(pos, static_cast<int>(dimId), region);

        if (isLocked) {
            // 箱子已被锁定
            bool isOwner = (ownerUuid == player_uuid);
            std::vector<std::string> sharedPlayers = getSharedPlayers(pos, static_cast<int>(dimId));
            bool isSharedPlayer = false;
            for (const std::string& sharedPlayerUuid : sharedPlayers) {
                if (sharedPlayerUuid == player_uuid) {
                    isSharedPlayer = true;
                    break;
                }
            }

            if (isOwner) {
                // 玩家是箱子主人
                if (isHoldingStick) {
                    showChestLockForm(player, pos, static_cast<int>(dimId), isLocked, ownerUuid, chestType, region);
                    ev.cancel(); // 阻止默认打开箱子行为
                    return;
                } else {
                    // 主人未手持木棍，直接打开箱子
                    return; // 不取消事件，允许打开
                }
            } else if (isSharedPlayer) {
                // 玩家是分享玩家
                if (isHoldingStick) {
                    player.sendMessage("§e你已被分享此箱子，但只有主人才能使用木棍管理箱子。");
                } else {
                    // 分享玩家未手持木棍
                    if (chestType == ChestType::Shop) {
                        showShopChestItemsForm(player, pos, static_cast<int>(dimId), region);
                    } else if (chestType == ChestType::Public) {
                        // 公共箱子，分享玩家可以直接打开
                        return; // 不取消事件，允许打开
                    } else {
                        // 其他类型的箱子，分享玩家弹出提示信息，阻止打开
                        player.sendMessage("§e你已被分享此箱子，但无法直接打开，请联系主人。");
                    }
                }
                ev.cancel(); // 阻止默认打开箱子行为
                return;
            } else {
                // 玩家既不是箱子主人也不是分享玩家
                if (chestType == ChestType::Shop) {
                    showShopChestItemsForm(player, pos, static_cast<int>(dimId), region);
                } else if (chestType == ChestType::Public) {
                    // 公共箱子，允许任何玩家打开
                    return; // 不取消事件，允许打开
                } else {
                    // 其他类型的锁定箱子，阻止打开
                    player.sendMessage("§c这个箱子已经被锁定了，你不是它的主人，也没有被分享！");
                }
                ev.cancel(); // 阻止默认打开箱子行为
                return;
            }
        } else {
            // 箱子未被锁定
            if (SynchedActorDataAccess::getActorFlag(player.getEntityContext(), ActorFlags::Sneaking) && isHoldingStick) {
                showChestLockForm(player, pos, static_cast<int>(dimId), isLocked, ownerUuid, chestType, region);
                ev.cancel(); // 阻止默认打开箱子行为
                return;
            }
            // 未锁定且没有手持木棍或未潜行，允许打开箱子
            return;
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
                    logger.debug(
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
