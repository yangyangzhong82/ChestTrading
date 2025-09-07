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


std::unordered_set<::BlockPos>* affectedBlocks = nullptr;
BlockSource*                    region         = nullptr;
LL_AUTO_TYPE_INSTANCE_HOOK(Test1, ll::memory::HookPriority::Normal, Explosion, &Explosion::explode, bool) {
    affectedBlocks = &mAffectedBlocks.get();
    region         = &mRegion;
    auto res       = origin();
    affectedBlocks = nullptr;
    return res;
}

LL_AUTO_TYPE_INSTANCE_HOOK(
    Test2,
    ll::memory::HookPriority::Normal,
    ExplosionStartedEvent,
    &ExplosionStartedEvent::$dtor,
    void
) {
    std::unordered_set<BlockPos> replaced;
    for (const auto& pos : *affectedBlocks) {
        // 检查方块是否是箱子
        bool isBlockChest      = (region->getBlock(pos).getTypeName() == "minecraft:chest");
        bool isExtraBlockChest = (region->getExtraBlock(pos).getTypeName() == "minecraft:chest");

        if (isBlockChest || isExtraBlockChest) {
            // 如果是箱子，则检查它是否被锁定
            auto [locked, ownerUuid] = isChestLocked(pos, static_cast<int>(region->getDimensionId()));
            if (locked) {
                // 如果是上锁的箱子，则跳过它，不将其添加到被替换的方块列表中，从而保护它不被摧毁
                continue;
            }
        }
        // 如果不是箱子，或者箱子未被锁定，则将其添加到被替换的方块列表中，使其可以被爆炸摧毁
        replaced.emplace(pos);
    }
}
}