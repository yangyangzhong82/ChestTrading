#include "chestprotect.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/events/ExplosionStartedEvent.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Explosion.h"
#include "mc/world/level/block/Block.h"

namespace CT {

std::unordered_set<::BlockPos>* affectedBlocks = nullptr;
BlockSource*                    region         = nullptr;
LL_AUTO_TYPE_INSTANCE_HOOK(
    Test1,
    ll::memory::HookPriority::Normal,
    Explosion,
    &Explosion::explode,
    bool,
    ::IRandom& random
) {
    affectedBlocks = &mAffectedBlocks.get();
    region         = &mRegion;
    auto res       = origin(random);
    affectedBlocks = nullptr;
    return res;
}

//mc对爆炸处理是单线程，暂不用考虑并发问题
LL_AUTO_TYPE_INSTANCE_HOOK(
    Test2,
    ll::memory::HookPriority::Normal,
    ExplosionStartedEvent,
    &ExplosionStartedEvent::$dtor,
    void
) {
    std::unordered_set<BlockPos> replaced;
    for (const auto& pos : *affectedBlocks) {
        // 检查主方块是否是箱子
        if (region->getBlock(pos).getTypeName() == "minecraft:chest") {
            // 检查是否是上锁的箱子
            int dimId = static_cast<int>(region->getDimensionId());
            auto [locked, ownerUuid, chestType] = CT::getChestDetails(pos, dimId, *region);
            if (locked) {
                // 如果是上锁的箱子，则跳过，不添加到 replaced 中
                continue;
            }
        }
        // 检查额外方块是否是箱子
        if (region->getExtraBlock(pos).getTypeName() == "minecraft:chest") {
            // 检查是否是上锁的箱子
            int dimId = static_cast<int>(region->getDimensionId());
            auto [locked, ownerUuid, chestType] = CT::getChestDetails(pos, dimId, *region);
            if (locked) {
                // 如果是上锁的箱子，则跳过，不添加到 replaced 中
                continue;
            }
        }
        replaced.emplace(pos);
    }
    *affectedBlocks = replaced;
}

} // namespace CT
