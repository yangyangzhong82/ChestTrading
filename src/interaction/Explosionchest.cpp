#include "chestprotect.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/events/ExplosionStartedEvent.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Explosion.h"
#include "mc/world/level/block/Block.h"

namespace CT {
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
            auto [locked, ownerUuid, chestType] = getChestDetails(pos, static_cast<int>(region->getDimensionId()));
            if (locked) {
                // 如果是上锁的箱子，则跳过它，不将其添加到被替换的方块列表中，从而保护它不被摧毁
                continue;
            }
        }
        // 如果不是箱子，或者箱子未被锁定，则将其添加到被替换的方块列表中，使其可以被爆炸摧毁
        replaced.emplace(pos);
        *affectedBlocks = replaced;
    }
}
} // namespace CT
