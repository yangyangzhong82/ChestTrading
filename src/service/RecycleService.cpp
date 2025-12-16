#include "RecycleService.h"
#include "FloatingText/FloatingText.h"
#include "db/Sqlite3Wrapper.h"
#include "logger.h"
#include "repository/ItemRepository.h"
#include "repository/ShopRepository.h"

namespace CT {

RecycleService& RecycleService::getInstance() {
    static RecycleService instance;
    return instance;
}

SetCommissionResult RecycleService::setCommission(
    BlockPos           pos,
    int                dimId,
    const std::string& itemNbt,
    double             price,
    int                minDurability,
    const std::string& requiredEnchants,
    int                maxRecycleCount
) {
    auto& itemRepo = ItemRepository::getInstance();
    int   itemId   = itemRepo.getOrCreateItemId(itemNbt);
    if (itemId < 0) {
        return {false, "无法创建物品定义", -1};
    }

    RecycleItemData data;
    data.dimId            = dimId;
    data.pos              = pos;
    data.itemId           = itemId;
    data.price            = price;
    data.minDurability    = minDurability;
    data.requiredEnchants = requiredEnchants;
    data.maxRecycleCount  = maxRecycleCount;

    auto& shopRepo = ShopRepository::getInstance();
    if (shopRepo.upsertRecycleItem(data)) {
        FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, ChestType::RecycleShop);
        return {true, "", itemId};
    }
    return {false, "数据库操作失败", -1};
}

bool RecycleService::updateCommission(BlockPos pos, int dimId, int itemId, double price, int maxRecycleCount) {
    auto& shopRepo = ShopRepository::getInstance();
    if (shopRepo.updateRecycleItem(pos, dimId, itemId, price, maxRecycleCount)) {
        FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, ChestType::RecycleShop);
        return true;
    }
    return false;
}

std::vector<RecycleItemData> RecycleService::getCommissions(BlockPos pos, int dimId) {
    return ShopRepository::getInstance().findAllRecycleItems(pos, dimId);
}

std::optional<RecycleItemData> RecycleService::getCommission(BlockPos pos, int dimId, int itemId) {
    return ShopRepository::getInstance().findRecycleItem(pos, dimId, itemId);
}

std::vector<RecycleRecordData> RecycleService::getRecycleRecords(BlockPos pos, int dimId, int itemId, int limit) {
    return ShopRepository::getInstance().getRecycleRecords(pos, dimId, itemId, limit);
}

RecycleResult RecycleService::executeRecycle(
    Player&            recycler,
    BlockPos           pos,
    int                dimId,
    int                itemId,
    int                quantity,
    const std::string& ownerUuid,
    BlockSource&       region
) {
    auto& shopRepo = ShopRepository::getInstance();
    auto& db       = Sqlite3Wrapper::getInstance();

    // 更新已回收数量
    Transaction txn(db);
    if (!txn.isActive()) {
        return {false, "数据库事务启动失败", 0, 0.0};
    }

    if (!shopRepo.incrementRecycledCount(pos, dimId, itemId, quantity)) {
        return {false, "更新回收数量失败", 0, 0.0};
    }

    auto commission = shopRepo.findRecycleItem(pos, dimId, itemId);
    if (!commission) {
        return {false, "找不到回收委托", 0, 0.0};
    }

    double totalPrice = commission->price * quantity;

    RecycleRecordData record;
    record.dimId        = dimId;
    record.pos          = pos;
    record.itemId       = itemId;
    record.recyclerUuid = recycler.getUuid().asString();
    record.recycleCount = quantity;
    record.totalPrice   = totalPrice;

    if (!shopRepo.addRecycleRecord(record)) {
        return {false, "添加回收记录失败", 0, 0.0};
    }

    if (!txn.commit()) {
        return {false, "数据库更新失败", 0, 0.0};
    }

    return {true, "", quantity, totalPrice};
}

} // namespace CT