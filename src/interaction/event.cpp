#include "event.h"
#include "db/Sqlite3Wrapper.h"
#include "form/LockForm.h"
#include "form/RecycleForm.h"
#include "form/ShopForm.h"

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


#include "Bedrock-Authority/permission/PermissionManager.h"
#include "mc\world\actor\provider\SynchedActorDataAccess.h"
#include <chrono>


namespace CT {

// 定义一个全局map来存储每个玩家的上次交互时间
std::map<std::string, std::chrono::steady_clock::time_point> lastInteractionTime;
std::mutex                                                   lastInteractionTimeMutex; // 保护 lastInteractionTime
// 定义防抖间隔，例如500毫秒
const std::chrono::milliseconds DEBOUNCE_INTERVAL(500);
// 清理间隔（60秒未交互的条目将被清理）
const std::chrono::seconds CLEANUP_THRESHOLD(60);

void registerEventListener() {
    auto& eventBus = ll::event::EventBus::getInstance();

    eventBus.emplaceListener<ll::event::PlayerInteractBlockEvent>([](ll::event::PlayerInteractBlockEvent& ev) {
        auto block = ev.block();

        // 立即检查是否为箱子，避免不必要的处理
        if (block->getTypeName() != "minecraft:chest") {
            return;
        }

        auto&       player         = ev.self();
        auto&       item           = player.getCarriedItem();
        auto        dimId          = player.getDimensionId();
        auto        originalPos    = ev.blockPos();
        bool        isHoldingStick = (item.getTypeName() == "minecraft:stick");
        std::string player_uuid    = player.getUuid().asString();
        auto&       region         = player.getDimensionBlockSource();

        // 防抖处理（仅对箱子交互）
        auto currentTime = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(lastInteractionTimeMutex);
            // 清理过期条目
            for (auto it = lastInteractionTime.begin(); it != lastInteractionTime.end();) {
                if (currentTime - it->second > CLEANUP_THRESHOLD) {
                    it = lastInteractionTime.erase(it);
                } else {
                    ++it;
                }
            }

            if (lastInteractionTime.count(player_uuid)
                && std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastInteractionTime[player_uuid])
                       < DEBOUNCE_INTERVAL) {
                ev.cancel();
                return;
            }
            lastInteractionTime[player_uuid] = currentTime;
        }

        // 始终使用主箱子位置进行逻辑判断
        BlockPos pos = CT::internal::GetMainChestPos(originalPos, region);

        auto [isLocked, ownerUuid, chestType] = getChestDetails(pos, static_cast<int>(dimId), region);
        bool isAdmin = BA::permission::PermissionManager::getInstance().hasPermission(player_uuid, "chest.admin");
        bool isOwner = (ownerUuid == player_uuid) || isAdmin;

        // 玩家手持木棍：打开管理菜单
        if (isHoldingStick) {
            // 只有主人才能用木棍管理
            if (isLocked && !isOwner) {
                player.sendMessage("§c只有箱子主人才能使用木棍管理。");
            } else {
                showChestLockForm(player, pos, static_cast<int>(dimId), isLocked, ownerUuid, chestType, region);
            }
            ev.cancel();
            return;
        }

        // 玩家没有手持木棍：尝试打开箱子
        if (canPlayerOpenChest(player_uuid, pos, static_cast<int>(dimId), region) || isAdmin) {
            // 权限检查通过，允许打开
            // 如果是商店或回收商店，主人和管理员可以直接打开箱子放入物品
            if (chestType == ChestType::Shop || chestType == ChestType::RecycleShop) {
                if (isOwner) {
                    // 主人或管理员直接打开箱子
                    return;
                }
                // 非主人显示商店表单
                if (chestType == ChestType::Shop) {
                    showShopChestItemsForm(player, pos, static_cast<int>(dimId), region);
                } else {
                    showRecycleForm(player, pos, static_cast<int>(dimId), region);
                }
                ev.cancel();
                return;
            }
            // 其他情况（普通箱子/公共箱子），直接打开
            return;
        } else {
            // 权限检查失败
            // 如果是商店或回收商店，非分享者也可以查看
            if (chestType == ChestType::Shop) {
                showShopChestItemsForm(player, pos, static_cast<int>(dimId), region);
            } else if (chestType == ChestType::RecycleShop) {
                showRecycleForm(player, pos, static_cast<int>(dimId), region);
            } else {
                player.sendMessage("§c这个箱子已经被锁定了，你无法打开。");
            }
            ev.cancel();
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
                    bool isAdmin = BA::permission::PermissionManager::getInstance().hasPermission(
                        player.getUuid().asString(),
                        "chest.admin"
                    );
                    if (isAdmin) {
                        CT::removeChest(pos, dimId, region);
                        player.sendMessage("§a管理员权限：已移除箱子锁定数据。");
                        return;
                    }
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
