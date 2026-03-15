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

std::unordered_map<std::string, int>
BaseTransactionService::countAllMatchingItems(ChestBlockActor* chest, const std::vector<std::string>& itemNbtList) {
    std::unordered_map<std::string, int> result;
    if (!chest || itemNbtList.empty()) return result;

    // 预解析所有目标物品的 binary key
    struct ItemKey {
        std::string                nbt;
        std::optional<std::string> binKey;
        bool                       damageable = false;
    };
    std::vector<ItemKey> keys;
    keys.reserve(itemNbtList.size());
    for (const auto& nbt : itemNbtList) {
        ItemKey k;
        k.nbt = nbt;
        if (auto expectedItem = FormUtils::createItemStackFromNbtString(nbt)) {
            k.damageable = expectedItem->isDamageableItem();
        }
        if (auto tag = NbtUtils::parseSNBT(nbt)) {
            auto cleaned = NbtUtils::cleanNbtForComparison(*tag, k.damageable);
            k.binKey     = NbtUtils::toBinaryNBT(*cleaned);
        }
        result[nbt] = 0;
        keys.push_back(std::move(k));
    }

    // 单次遍历箱子所有格子
    for (int i = 0; i < chest->getContainerSize(); ++i) {
        const auto& chestItem = chest->getItem(i);
        if (chestItem.isNull()) continue;

        auto chestItemNbt = NbtUtils::getItemNbt(chestItem);
        if (!chestItemNbt) continue;

        auto        cleanedNbt = NbtUtils::cleanNbtForComparison(*chestItemNbt, chestItem.isDamageableItem());
        std::string chestBinKey;
        std::string chestSnbt;

        for (const auto& k : keys) {
            bool matches = false;
            if (k.binKey) {
                if (chestBinKey.empty()) {
                    chestBinKey = NbtUtils::toBinaryNBT(*cleanedNbt);
                }
                matches = (chestBinKey == *k.binKey);
            } else {
                if (chestSnbt.empty()) {
                    chestSnbt = NbtUtils::toSNBT(*cleanedNbt);
                }
                matches = (chestSnbt == k.nbt);
            }
            if (matches) {
                result[k.nbt] += chestItem.mCount;
                break; // 一个格子只匹配一种物品
            }
        }
    }

    return result;
}

} // namespace CT