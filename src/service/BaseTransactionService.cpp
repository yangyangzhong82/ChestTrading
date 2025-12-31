#include "BaseTransactionService.h"
#include "Utils/NbtUtils.h"
#include "form/FormUtils.h"

namespace CT {

ChestBlockActor* BaseTransactionService::getChestActor(BlockSource& region, BlockPos pos) {
    auto* blockActor = region.getBlockEntity(pos);
    if (!blockActor || blockActor->mType != BlockActorType::Chest) {
        return nullptr;
    }
    return static_cast<ChestBlockActor*>(blockActor);
}

int BaseTransactionService::removeItemsFromChest(ChestBlockActor* chest, const std::string& itemNbt, int count) {
    if (!chest) return 0;

    int actualRemoved = 0;
    for (int i = 0; i < chest->getContainerSize() && actualRemoved < count; ++i) {
        const auto& chestItem = chest->getItem(i);
        if (chestItem.isNull()) continue;

        auto chestItemNbt = NbtUtils::getItemNbt(chestItem);
        if (!chestItemNbt) continue;

        auto        cleanedNbt    = NbtUtils::cleanNbtForComparison(*chestItemNbt);
        std::string currentNbtStr = NbtUtils::toSNBT(*cleanedNbt);

        if (currentNbtStr == itemNbt) {
            int removeCount = std::min(count - actualRemoved, (int)chestItem.mCount);
            chest->removeItem(i, removeCount);
            actualRemoved += removeCount;
        }
    }
    return actualRemoved;
}

bool BaseTransactionService::addItemsToChest(ChestBlockActor* chest, const std::string& itemNbt, int count) {
    if (!chest) return false;

    auto itemPtr = FormUtils::createItemStackFromNbtString(itemNbt);
    if (!itemPtr) return false;

    ItemStack item = *itemPtr;
    item.mCount    = count;
    chest->addItem(item);
    return true;
}

int BaseTransactionService::countMatchingItems(ChestBlockActor* chest, const std::string& itemNbt) {
    if (!chest) return 0;

    int total = 0;
    for (int i = 0; i < chest->getContainerSize(); ++i) {
        const auto& chestItem = chest->getItem(i);
        if (chestItem.isNull()) continue;

        auto chestItemNbt = NbtUtils::getItemNbt(chestItem);
        if (!chestItemNbt) continue;

        auto cleanedNbt = NbtUtils::cleanNbtForComparison(*chestItemNbt);
        if (NbtUtils::toSNBT(*cleanedNbt) == itemNbt) {
            total += chestItem.mCount;
        }
    }
    return total;
}

} // namespace CT
