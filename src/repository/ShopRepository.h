#pragma once

#include "mc/world/level/BlockPos.h"
#include <optional>
#include <string>
#include <vector>

namespace CT {

// 商店物品数据结构
struct ShopItemData {
    int         dimId;
    BlockPos    pos;
    int         itemId;
    std::string itemNbt;
    double      price;
    int         dbCount;
    int         slot;
};

// 回收商店物品数据结构
struct RecycleItemData {
    int         dimId;
    BlockPos    pos;
    int         itemId;
    std::string itemNbt;
    double      price;
    int         minDurability;
    std::string requiredEnchants;
    int         maxRecycleCount;
    int         currentRecycledCount;
    int         requiredAuxValue = -1; // -1 表示不筛选特殊值
};

// 回收记录数据结构
struct RecycleRecordData {
    int         id;
    int         dimId;
    BlockPos    pos;
    int         itemId;
    std::string recyclerUuid;
    int         recycleCount;
    double      totalPrice;
    std::string timestamp;
};

// 购买记录数据结构
struct PurchaseRecordData {
    int         id;
    int         dimId;
    BlockPos    pos;
    int         itemId;
    std::string buyerUuid;
    int         purchaseCount;
    double      totalPrice;
    std::string timestamp;
    std::string itemNbt; // 关联的物品NBT
};

// 公开商店物品数据（包含商店信息）
struct PublicShopItemData {
    int         dimId;
    BlockPos    pos;
    std::string ownerUuid;
    std::string shopName;
    int         itemId;
    std::string itemNbt;
    double      price;
    int         dbCount;
    bool        isOfficial; // 是否官方商店
};

// 公开回收商店物品数据
struct PublicRecycleItemData {
    int         dimId;
    BlockPos    pos;
    std::string ownerUuid;
    std::string shopName;
    int         itemId;
    std::string itemNbt;
    double      price;
    bool        isOfficial;
};

// 箱子销量统计数据
struct ChestSalesData {
    int         dimId;
    BlockPos    pos;
    std::string ownerUuid;
    std::string shopName;
    int         totalSalesCount;
    double      totalRevenue;
    std::string lastSaleTime;
};

/**
 * @brief 商店数据访问层
 */
class ShopRepository {
public:
    static ShopRepository& getInstance();

    ShopRepository(const ShopRepository&)            = delete;
    ShopRepository& operator=(const ShopRepository&) = delete;

    // === 商品CRUD ===
    bool                        upsertItem(const ShopItemData& item);
    bool                        removeItem(BlockPos pos, int dimId, int itemId);
    bool                        removeAllItems(BlockPos pos, int dimId);
    std::optional<ShopItemData> findItem(BlockPos pos, int dimId, int itemId);
    std::vector<ShopItemData>   findAllItems(BlockPos pos, int dimId);

    // === 库存管理 ===
    bool updateDbCount(BlockPos pos, int dimId, int itemId, int newCount);
    bool decrementDbCount(BlockPos pos, int dimId, int itemId, int amount);

    // === 购买记录 ===
    bool                            addPurchaseRecord(const PurchaseRecordData& record);
    std::vector<PurchaseRecordData> getPurchaseRecords(BlockPos pos, int dimId, int limit = 50);
    std::vector<PurchaseRecordData> getPlayerPurchaseHistory(const std::string& playerUuid, int limit = 50);

    // === 回收商店 ===
    std::vector<RecycleItemData>   findAllRecycleItems(BlockPos pos, int dimId);
    std::optional<RecycleItemData> findRecycleItem(BlockPos pos, int dimId, int itemId);
    bool                           upsertRecycleItem(const RecycleItemData& item);
    bool                           updateRecycleItem(BlockPos pos, int dimId, int itemId, double price, int maxCount);
    bool                           incrementRecycledCount(BlockPos pos, int dimId, int itemId, int amount);
    bool                           addRecycleRecord(const RecycleRecordData& record);
    std::vector<RecycleRecordData> getRecycleRecords(BlockPos pos, int dimId, int itemId, int limit = 50);

    // === 公开商店物品查询 ===
    std::vector<PublicShopItemData>    findAllPublicShopItems();
    std::vector<PublicRecycleItemData> findAllPublicRecycleItems();

    // === 销量统计 ===
    std::vector<ChestSalesData> getChestSalesRanking(int limit = 50);

private:
    ShopRepository() = default;
};

} // namespace CT