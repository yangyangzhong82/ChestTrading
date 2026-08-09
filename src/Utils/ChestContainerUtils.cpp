#include "ChestContainerUtils.h"

#include "mc/world/Container.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"

namespace CT::ChestContainerUtils {

Container* tryGetChestContainer(BlockSource& region, BlockPos const& pos) {
    if (!region.hasChunksAt(pos, 0, false)) {
        return nullptr;
    }

    auto* blockActor = region.getBlockEntity(pos);
    if (!blockActor || blockActor->mType != BlockActorType::Chest) {
        return nullptr;
    }

    auto*    chest     = static_cast<ChestBlockActor*>(blockActor);
    bool     wasPaired = chest->mLargeChestPaired != nullptr;
    BlockPos pairedPos = pos;
    if (wasPaired) {
        pairedPos = chest->mLargeChestPairedPosition;
        if (!region.hasChunksAt(pairedPos, 0, false)) {
            return nullptr;
        }
    }

    chest->_validatePairedChest(region);

    // Pair validation can clear a stale pointer while a chest is being unpaired.
    // Defer this read for one update instead of observing a transient half-container.
    if (wasPaired && !chest->mLargeChestPaired) {
        return nullptr;
    }

    if (chest->mLargeChestPaired && !chest->mPairLead) {
        auto* pairedBlockActor = region.getBlockEntity(pairedPos);
        if (!pairedBlockActor || pairedBlockActor->mType != BlockActorType::Chest) {
            return nullptr;
        }

        auto* pairedChest = static_cast<ChestBlockActor*>(pairedBlockActor);
        pairedChest->_validatePairedChest(region);
        if (!pairedChest->mPairLead || pairedChest->mLargeChestPaired != chest) {
            return nullptr;
        }
        chest = pairedChest;
    }

    return chest->getContainer();
}

} // namespace CT::ChestContainerUtils
