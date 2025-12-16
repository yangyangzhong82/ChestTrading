#include "ShopService.h"
#include "ChestService.h"
#include "FloatingText/FloatingText.h"
#include "Utils/MoneyFormat.h"
#include "Utils/NbtUtils.h"
#include "Utils/economy.h"
#include "form/FormUtils.h"
#include "ll/api/service/PlayerInfo.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "repository/ItemRepository.h"
#include "repository/ShopRepository.h"

namespace CT {

ShopService& ShopService::getInstance() {
    static ShopService instance;
    return instance;
}

SetPriceResult ShopService::setItemPrice(
    BlockPos           pos,
    int                dimId,
    const std::string& itemNbt,
    double             price,
    int                initialCount,
    BlockSource&       region
) {
    BlockPos mainPos = ChestService::getInstance().getMainChestPos(pos, region);

    // 获取或创建物品ID
    int itemId = ItemRepository::getInstance().getOrCreateItemId(itemNbt);
    if (itemId < 0) {
        return {false, "§c无法创建物品定义！", -1};
    }

    // 创建商品数据
    ShopItemData data;
    data.dimId   = dimId;
    data.pos     = mainPos;
    data.itemId  = itemId;
    data.price   = price;
    data.dbCount = initialCount;
    data.slot    = 0;

    if (!ShopRepository::getInstance().upsertItem(data)) {
        return {false, "§c物品价格设置失败！", -1};
    }

    // 更新悬浮字
    FloatingTextManager::getInstance().updateShopFloatingText(mainPos, dimId, ChestType::Shop);

    return {
        true,
        "§a物品价格设置成功！价格: " + MoneyFormat::format(price) + "，数量: " + std::to_string(initialCount),
        itemId
    };
}

bool ShopService::removeItem(BlockPos pos, int dimId, int itemId, BlockSource& region) {
    BlockPos mainPos = ChestService::getInstance().getMainChestPos(pos, region);
    bool     success = ShopRepository::getInstance().removeItem(mainPos, dimId, itemId);
    if (success) {
        FloatingTextManager::getInstance().updateShopFloatingText(mainPos, dimId, ChestType::Shop);
    }
    return success;
}

std::vector<ShopItemData> ShopService::getShopItems(BlockPos pos, int dimId, BlockSource& region) {
    BlockPos mainPos = ChestService::getInstance().getMainChestPos(pos, region);
    return ShopRepository::getInstance().findAllItems(mainPos, dimId);
}

PurchaseResult ShopService::purchaseItem(
    Player&            buyer,
    BlockPos           pos,
    int                dimId,
    int                itemId,
    int                quantity,
    BlockSource&       region,
    const std::string& itemNbt
) {
    BlockPos mainPos = ChestService::getInstance().getMainChestPos(pos, region);

    // 获取商品信息
    auto itemOpt = ShopRepository::getInstance().findItem(mainPos, dimId, itemId);
    if (!itemOpt) {
        return {false, "§c商品不存在！"};
    }

    double totalPrice = quantity * itemOpt->price;

    // 检查金钱
    if (!Economy::hasMoney(buyer, totalPrice)) {
        return {false, "§c你的金币不足！需要 §6" + MoneyFormat::format(totalPrice) + "§c 金币。"};
    }

    // 检查箱子库存
    int actualAvailable = countItemsInChest(region, mainPos, dimId, itemNbt);
    if (actualAvailable < quantity) {
        return {false, "§c箱子中没有足够的商品！实际库存: " + std::to_string(actualAvailable)};
    }

    // 从箱子移除物品
    int actualRemoved = removeItemsFromChest(region, mainPos, itemNbt, quantity);
    if (actualRemoved < quantity) {
        // 放回已移除的物品
        if (actualRemoved > 0) {
            addItemsToChest(region, mainPos, itemNbt, actualRemoved);
        }
        return {false, "§c购买失败，箱子中物品数量不足！"};
    }

    // 扣钱
    if (!Economy::reduceMoney(buyer, totalPrice)) {
        // 放回物品
        addItemsToChest(region, mainPos, itemNbt, quantity);
        return {false, "§c购买失败，金币扣除失败。"};
    }

    // 更新数据库库存
    ShopRepository::getInstance().decrementDbCount(mainPos, dimId, itemId, quantity);

    // 给店主加钱
    auto chestInfo = ChestService::getInstance().getChestInfo(mainPos, dimId, region);
    if (chestInfo && !chestInfo->ownerUuid.empty()) {
        Economy::addMoneyByUuid(chestInfo->ownerUuid, totalPrice);
    }

    // 给玩家物品
    auto itemPtr = FormUtils::createItemStackFromNbtString(itemNbt);
    if (itemPtr) {
        int maxStackSize    = itemPtr->getMaxStackSize();
        int remainingToGive = quantity;
        while (remainingToGive > 0) {
            int       giveCount  = std::min(remainingToGive, maxStackSize);
            ItemStack itemToGive = *itemPtr;
            itemToGive.mCount    = giveCount;
            if (!buyer.add(itemToGive)) {
                buyer.drop(itemToGive, true);
            }
            remainingToGive -= giveCount;
        }
        buyer.refreshInventory();
    }

    // 记录购买
    PurchaseRecordData record;
    record.dimId         = dimId;
    record.pos           = mainPos;
    record.itemId        = itemId;
    record.buyerUuid     = buyer.getUuid().asString();
    record.purchaseCount = quantity;
    record.totalPrice    = totalPrice;
    ShopRepository::getInstance().addPurchaseRecord(record);

    // 更新悬浮字
    FloatingTextManager::getInstance().updateShopFloatingText(mainPos, dimId, ChestType::Shop);

    return {true, "§a购买成功！", quantity, totalPrice};
}

int ShopService::countItemsInChest(BlockSource& region, BlockPos pos, int dimId, const std::string& itemNbt) {
    return FormUtils::countItemsInChest(region, pos, dimId, itemNbt);
}

int ShopService::removeItemsFromChest(BlockSource& region, BlockPos pos, const std::string& itemNbt, int count) {
    auto* blockActor = region.getBlockEntity(pos);
    if (!blockActor || blockActor->mType != BlockActorType::Chest) {
        return 0;
    }

    auto* chest         = static_cast<ChestBlockActor*>(blockActor);
    int   actualRemoved = 0;

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

bool ShopService::addItemsToChest(BlockSource& region, BlockPos pos, const std::string& itemNbt, int count) {
    auto* blockActor = region.getBlockEntity(pos);
    if (!blockActor || blockActor->mType != BlockActorType::Chest) {
        return false;
    }

    auto* chest   = static_cast<ChestBlockActor*>(blockActor);
    auto  itemPtr = FormUtils::createItemStackFromNbtString(itemNbt);
    if (!itemPtr) {
        return false;
    }

    ItemStack item = *itemPtr;
    item.mCount    = count;
    chest->addItem(item);
    return true;
}

} // namespace CT