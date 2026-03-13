#pragma once

#include "BaseTransactionService.h"
#include "mc/world/actor/player/Player.h"
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
class ShopService : public BaseTransactionService {
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
        BlockSource&       region,
        const std::string& actorUuid = ""
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

    // 将数据库库存与箱子实际库存对齐；传入 items 可复用已查询结果避免重复查询。
    bool syncDbStockWithChest(
        BlockPos                   pos,
        int                        dimId,
        BlockSource&               region,
        std::vector<ShopItemData>* items = nullptr
    );
};

} // namespace CT
