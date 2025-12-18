#include "event.h"
#include "Utils/NbtUtils.h"
#include "command/command.h"
#include "db/Sqlite3Wrapper.h"
#include "form/LockForm.h"
#include "form/RecycleForm.h"
#include "form/ShopForm.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "logger.h"
#include "mc/nbt/CompoundTagVariant.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Explosion.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "service/ChestService.h"
#include "service/TextService.h"


#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/game_refs/WeakRef.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/events/ActorEventCoordinator.h"
#include "mc/world/events/ActorGameplayEvent.h"
#include "mc/world/events/ActorGriefingBlockEvent.h"

#include "ll/api/event/player/PlayerDestroyBlockEvent.h"
#include "mc/world/actor/Hopper.h"
#include "mc/world/actor/boss/WitherBoss.h"
#include "mc/world/actor/monster/EnderDragon.h"
#include "mc/world/level/block/HopperBlock.h"
#include "mc/world/level/block/actor/PistonBlockActor.h"


#include "Bedrock-Authority/permission/PermissionManager.h"
#include "mc/world/actor/provider/SynchedActorDataAccess.h"
#include <chrono>


namespace CT {

// 每个玩家的上次交互时间
std::map<std::string, std::chrono::steady_clock::time_point> lastInteractionTime;
std::mutex                                                   lastInteractionTimeMutex; // 保护 lastInteractionTime
// 防抖间隔，
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

        // 打包箱子模式处理
        if (isInPackChestMode(player_uuid)) {
            ev.cancel();
            setPackChestMode(player_uuid, false);

            auto&    chestService = ChestService::getInstance();
            BlockPos packPos      = chestService.getMainChestPos(originalPos, region);

            // 检查特殊箱子权限
            auto packChestInfo = chestService.getChestInfo(packPos, static_cast<int>(dimId), region);
            if (packChestInfo.has_value()) {
                // 是特殊箱子，检查是否是主人
                if (packChestInfo->ownerUuid != player_uuid) {
                    player.sendMessage("§c你不是这个箱子的主人，无法打包。");
                    return;
                }
                // 是主人，先移除箱子设置
                chestService.removeChest(packPos, static_cast<int>(dimId), region);
            }

            // 获取箱子方块实体
            auto* blockActor = region.getBlockEntity(originalPos);
            if (!blockActor) {
                player.sendMessage("§c无法获取箱子数据。");
                return;
            }

            // 获取箱子NBT
            auto chestNbt = NbtUtils::getBlockEntityNbt(blockActor);
            if (!chestNbt || !chestNbt->contains("Items")) {
                player.sendMessage("§c箱子为空或无法读取。");
                return;
            }

            // 创建箱子物品NBT
            CompoundTag itemNbt;
            itemNbt["Name"]        = StringTag("minecraft:chest");
            itemNbt["Count"]       = ByteTag(1);
            itemNbt["Damage"]      = ShortTag(0);
            itemNbt["WasPickedUp"] = ByteTag(0);

            // 创建tag子标签并迁移Items
            CompoundTag tagNbt;
            tagNbt["Items"] = chestNbt->at("Items").get<ListTag>();
            itemNbt["tag"]  = std::move(tagNbt);

            // 从NBT创建物品
            auto chestItem = NbtUtils::createItemFromNbt(itemNbt);
            if (!chestItem) {
                player.sendMessage("§c创建物品失败。");
                return;
            }

            // 先尝试给玩家物品，检查返回值
            if (!player.addAndRefresh(*chestItem)) {
                player.sendMessage("§c背包已满，无法打包箱子。");
                return;
            }

            // 清空箱子内容防止掉落
            auto* chestActor = static_cast<ChestBlockActor*>(blockActor);
            chestActor->clearInventory(0);

            // 移除方块
            region.removeBlock(originalPos, BlockChangeContext{});

            player.sendMessage("§a箱子已打包成物品！");
            return;
        }

        // 始终使用主箱子位置进行逻辑判断
        auto&    chestService = ChestService::getInstance();
        BlockPos pos          = chestService.getMainChestPos(originalPos, region);

        auto        chestInfo = chestService.getChestInfo(pos, static_cast<int>(dimId), region);
        bool        isLocked  = chestInfo.has_value();
        std::string ownerUuid = isLocked ? chestInfo->ownerUuid : "";
        ChestType   chestType = isLocked ? chestInfo->type : ChestType::Invalid;
        bool isAdmin = BA::permission::PermissionManager::getInstance().hasPermission(player_uuid, "chest.admin");
        bool isOwner = (ownerUuid == player_uuid) || isAdmin;

        auto& txt = TextService::getInstance();

        // 玩家手持木棍：打开管理菜单
        if (isHoldingStick) {
            // 只有主人才能用木棍管理
            if (isLocked && !isOwner) {
                player.sendMessage(txt.getMessage("chest.not_owner"));
            } else {
                showChestLockForm(player, pos, static_cast<int>(dimId), isLocked, ownerUuid, chestType, region);
            }
            ev.cancel();
            return;
        }

        // 玩家没有手持木棍：尝试打开箱子
        if (chestService.canPlayerAccess(player_uuid, pos, static_cast<int>(dimId), region) || isAdmin) {
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
                player.sendMessage(txt.getMessage("chest.locked"));
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
                auto& chestService = ChestService::getInstance();
                auto  chestInfo    = chestService.getChestInfo(pos, dimId, region);
                if (chestInfo) {
                    bool isAdmin = BA::permission::PermissionManager::getInstance().hasPermission(
                        player.getUuid().asString(),
                        "chest.admin"
                    );
                    if (isAdmin) {
                        chestService.removeChest(pos, dimId, region);
                        player.sendMessage("§a管理员权限：已移除箱子锁定数据。");
                        return;
                    }
                    event.cancel();
                    player.sendMessage("§c这个上锁的箱子不能被破坏！");
                    logger.debug(
                        "玩家 {} 尝试破坏被玩家 {} 锁定的箱子 ({}, {}, {}) in dim {}，已阻止。",
                        player.getUuid().asString(),
                        chestInfo->ownerUuid,
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
