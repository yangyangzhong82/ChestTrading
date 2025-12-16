#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "repository/ShopRepository.h"
#include <string>
#include <vector>

namespace CT {

// 购买结果
struct PurchaseResult {
    bool        success;
    std::string message;
    int         itemsReceived = 0;
    double      totalCost     = 0.0;
};

// 设置价格结果
struct SetPriceResult {
    bool        success;
    std::string message;
    int         itemId = -1;
};

/**
 * @brief 商店业务服务
 * 处理商品管理和交易逻辑
 */
class ShopService {
public:
    static ShopService& getInstance();

    ShopService(const ShopService&)            = delete;
    ShopService& operator=(const ShopService&) = delete;

    // === 商品管理 ===
    SetPriceResult setItemPrice(
        BlockPos           pos,
        int                dimId,
        const std::string& itemNbt,
        double             price,
        int                initialCount,
        BlockSource&       region
    );

    bool removeItem(BlockPos pos, int dimId, int itemId, BlockSource& region);

    std::vector<ShopItemData> getShopItems(BlockPos pos, int dimId, BlockSource& region);

    // === 交易 ===
    PurchaseResult purchaseItem(
        Player&            buyer,
        BlockPos           pos,
        int                dimId,
        int                itemId,
        int                quantity,
        BlockSource&       region,
        const std::string& itemNbt
    );

    // === 库存 ===
    int countItemsInChest(BlockSource& region, BlockPos pos, int dimId, const std::string& itemNbt);

private:
    ShopService() = default;

    // 从箱子中移除物品
    int removeItemsFromChest(BlockSource& region, BlockPos pos, const std::string& itemNbt, int count);

    // 给箱子添加物品
    bool addItemsToChest(BlockSource& region, BlockPos pos, const std::string& itemNbt, int count);
};

} // namespace CT