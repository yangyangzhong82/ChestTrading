#include "event.h"
#include "db/Sqlite3Wrapper.h" // 引入 Sqlite3Wrapper
#include "form/LockForm.h"
#include "interaction/chestprotect.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "logger.h"
#include "mc/platform/UUID.h" // 引入 UUID
#include "mc/world/level/BlockSource.h"
#include "mc\world\level\block\actor\ChestBlockActor.h"

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
            auto [locked, owner_uuid] = isChestLocked(pos, static_cast<int>(dimId));
            auto* blockActor           = region.getBlockEntity(pos);
            auto pairedChestBlockActor = static_cast<class ChestBlockActor*>(blockActor)->mLargeChestPaired;
            // 如果是双箱子，并且另一个箱子存在
            if (pairedChestBlockActor) {
                auto pairedChestPos = pairedChestBlockActor->mLargeChestPairedPosition; // 另一个箱子坐标
                auto [isPairedChestLocked, pairedChestOwnerUuid] = isChestLocked(pairedChestPos, static_cast<int>(dimId));

                // 如果当前箱子未锁定，但配对箱子已锁定，则以配对箱子的锁定状态为准
                if (!locked && isPairedChestLocked) {
                    locked = true;
                    owner_uuid = pairedChestOwnerUuid;
                }
                // 如果当前箱子已锁定，但配对箱子未锁定，则以当前箱子的锁定状态为准 (保持不变)
                // 如果两个都锁定，或者两个都未锁定，则保持原样
            }
            
            if (locked) {
                // 箱子已被锁定
                if (owner_uuid != player_uuid) {
                    // 玩家不是箱子主人
                    ev.cancel();
                    player.sendMessage("§c这个箱子已经被锁定了，你不是它的主人！");
                    logger.info(
                        "玩家 {} 尝试打开被玩家 {} 锁定的箱子 ({}, {}, {}) in dim {}",
                        player_uuid,
                        owner_uuid,
                        pos.x,
                        pos.y,
                        pos.z,
                        static_cast<int>(dimId)
                    );
                    return;
                }
                // 玩家是箱子主人
                if (isHoldingStick) {
                    showChestLockForm(player, pos, static_cast<int>(dimId));
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
                    showChestLockForm(player, pos, static_cast<int>(dimId));
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

} // namespace CT
