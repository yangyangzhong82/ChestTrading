#include "ShopService.h"
#include "ChestService.h"
#include "Config/ConfigManager.h"
#include "FloatingText/FloatingText.h"
#include "TextService.h"
#include "Utils/MoneyFormat.h"
#include "Utils/NbtUtils.h"
#include "Utils/economy.h"
#include "db/Sqlite3Wrapper.h"
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

    auto& txt = TextService::getInstance();

    // 获取或创建物品ID
    int itemId = ItemRepository::getInstance().getOrCreateItemId(itemNbt);
    if (itemId < 0) {
        return {false, txt.getMessage("shop.item_def_fail"), -1};
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
        return {false, txt.getMessage("shop.price_set_fail"), -1};
    }

    // 更新悬浮字
    FloatingTextManager::getInstance().updateShopFloatingText(mainPos, dimId, ChestType::Shop);

    return {
        true,
        txt.getMessage(
            "shop.price_set_success",
            {{"price", MoneyFormat::format(price)}, {"count", std::to_string(initialCount)}}
        ),
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
    auto&    txt     = TextService::getInstance();
    BlockPos mainPos = ChestService::getInstance().getMainChestPos(pos, region);

    // 获取商品信息
    auto itemOpt = ShopRepository::getInstance().findItem(mainPos, dimId, itemId);
    if (!itemOpt) {
        return {false, txt.getMessage("shop.item_not_exist")};
    }

    double totalPrice = quantity * itemOpt->price;

    // 检查金钱
    if (!Economy::hasMoney(buyer, totalPrice)) {
        return {
            false,
            txt.getMessage("shop.insufficient_money", {{"price", MoneyFormat::format(totalPrice)}}
             )
        };
    }

    // 检查箱子库存
    int actualAvailable = countItemsInChest(region, mainPos, dimId, itemNbt);
    if (actualAvailable < quantity) {
        return {
            false,
            txt.getMessage("shop.insufficient_stock", {{"stock", std::to_string(actualAvailable)}}
             )
        };
    }

    // 从箱子移除物品
    int actualRemoved = removeItemsFromChest(region, mainPos, itemNbt, quantity);
    if (actualRemoved < quantity) {
        // 放回已移除的物品
        if (actualRemoved > 0) {
            addItemsToChest(region, mainPos, itemNbt, actualRemoved);
        }
        return {false, txt.getMessage("shop.purchase_chest_fail")};
    }

    // 扣钱
    if (!Economy::reduceMoney(buyer, totalPrice)) {
        // 放回物品
        addItemsToChest(region, mainPos, itemNbt, quantity);
        return {false, txt.getMessage("shop.purchase_money_fail")};
    }

    // 使用事务更新数据库库存和记录购买
    auto&       db = Sqlite3Wrapper::getInstance();
    Transaction txn(db);
    if (!txn.isActive()) {
        // 事务启动失败，回滚金钱和物品
        Economy::addMoney(buyer, totalPrice);
        addItemsToChest(region, mainPos, itemNbt, quantity);
        return {false, txt.getMessage("shop.purchase_db_fail")};
    }

    // 更新数据库库存（检查返回值）
    if (!ShopRepository::getInstance().decrementDbCount(mainPos, dimId, itemId, quantity)) {
        // 扣库存失败，显式回滚事务
        txn.rollback();
        Economy::addMoney(buyer, totalPrice);
        addItemsToChest(region, mainPos, itemNbt, quantity);
        return {false, txt.getMessage("shop.purchase_db_fail")};
    }

    // 记录购买
    PurchaseRecordData record;
    record.dimId         = dimId;
    record.pos           = mainPos;
    record.itemId        = itemId;
    record.buyerUuid     = buyer.getUuid().asString();
    record.purchaseCount = quantity;
    record.totalPrice    = totalPrice;
    if (!ShopRepository::getInstance().addPurchaseRecord(record)) {
        // 记录失败，显式回滚事务
        txn.rollback();
        Economy::addMoney(buyer, totalPrice);
        addItemsToChest(region, mainPos, itemNbt, quantity);
        return {false, txt.getMessage("shop.purchase_db_fail")};
    }

    // 提交事务
    if (!txn.commit()) {
        // 提交失败，回滚
        Economy::addMoney(buyer, totalPrice);
        addItemsToChest(region, mainPos, itemNbt, quantity);
        return {false, txt.getMessage("shop.purchase_db_fail")};
    }

    // 给店主加钱（扣除税率）
    auto chestInfo = ChestService::getInstance().getChestInfo(mainPos, dimId, region);
    if (chestInfo && !chestInfo->ownerUuid.empty()) {
        double taxRate     = ConfigManager::getInstance().get().taxSettings.shopTaxRate;
        double ownerIncome = totalPrice * (1.0 - taxRate);
        Economy::addMoneyByUuid(chestInfo->ownerUuid, ownerIncome);
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

    // 更新悬浮字
    FloatingTextManager::getInstance().updateShopFloatingText(mainPos, dimId, ChestType::Shop);

    std::string itemName = itemPtr ? std::string(itemPtr->getName()) : txt.getMessage("shop.unknown_item");
    return {
        true,
        txt.getMessage(
            "shop.purchase_success",
            {{"price", MoneyFormat::format(totalPrice)}, {"item", itemName}, {"count", std::to_string(quantity)}}
        ),
        quantity,
        totalPrice
    };
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