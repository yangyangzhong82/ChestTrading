#include "BaseTransactionService.h"
#include "Utils/NbtUtils.h"
#include "form/FormUtils.h"

#include <optional>

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

    // 预解析目标物品的二进制 NBT key，避免在箱子遍历中反复 toSNBT()。
    std::optional<std::string> expectedBinKey;
    bool                       expectedDamageable = false;
    if (auto expectedItem = FormUtils::createItemStackFromNbtString(itemNbt)) {
        expectedDamageable = expectedItem->isDamageableItem();
    }
    if (auto tag = NbtUtils::parseSNBT(itemNbt)) {
        auto cleaned = NbtUtils::cleanNbtForComparison(*tag, expectedDamageable);
        expectedBinKey = NbtUtils::toBinaryNBT(*cleaned);
    }

    int actualRemoved = 0;
    for (int i = 0; i < chest->getContainerSize() && actualRemoved < count; ++i) {
        const auto& chestItem = chest->getItem(i);
        if (chestItem.isNull()) continue;

        auto chestItemNbt = NbtUtils::getItemNbt(chestItem);
        if (!chestItemNbt) continue;

        auto cleanedNbt = NbtUtils::cleanNbtForComparison(*chestItemNbt, chestItem.isDamageableItem());

        bool matches = false;
        if (expectedBinKey) {
            matches = (NbtUtils::toBinaryNBT(*cleanedNbt) == *expectedBinKey);
        } else {
            matches = (NbtUtils::toSNBT(*cleanedNbt) == itemNbt);
        }

        if (matches) {
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
    item.set(count);
    chest->addItem(item);
    return true;
}

int BaseTransactionService::countMatchingItems(ChestBlockActor* chest, const std::string& itemNbt) {
    if (!chest) return 0;

    // 同 removeItemsFromChest：预解析期望 key，减少循环内开销。
    std::optional<std::string> expectedBinKey;
    bool                       expectedDamageable = false;
    if (auto expectedItem = FormUtils::createItemStackFromNbtString(itemNbt)) {
        expectedDamageable = expectedItem->isDamageableItem();
    }
    if (auto tag = NbtUtils::parseSNBT(itemNbt)) {
        auto cleaned = NbtUtils::cleanNbtForComparison(*tag, expectedDamageable);
        expectedBinKey = NbtUtils::toBinaryNBT(*cleaned);
    }

    int total = 0;
    for (int i = 0; i < chest->getContainerSize(); ++i) {
        const auto& chestItem = chest->getItem(i);
        if (chestItem.isNull()) continue;

        auto chestItemNbt = NbtUtils::getItemNbt(chestItem);
        if (!chestItemNbt) continue;

        auto cleanedNbt = NbtUtils::cleanNbtForComparison(*chestItemNbt, chestItem.isDamageableItem());
        bool matches = false;
        if (expectedBinKey) {
            matches = (NbtUtils::toBinaryNBT(*cleanedNbt) == *expectedBinKey);
        } else {
            matches = (NbtUtils::toSNBT(*cleanedNbt) == itemNbt);
        }

        if (matches) {
            total += chestItem.mCount;
        }
    }
    return total;
}

} // namespace CT
