#pragma once

#include "BaseTransactionService.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "repository/ShopRepository.h"
#include <string>
#include <vector>

namespace CT {

// 回收结果
struct RecycleResult {
    bool        success;
    std::string message;
    int         itemsRecycled = 0;
    double      totalEarned   = 0.0;
};

// 转移记录，用于精确回滚
struct TransferRecord {
    int slot;
    int count;
};

// 设置委托结果
struct SetCommissionResult {
    bool        success;
    std::string message;
    int         itemId = -1;
};

/**
 * @brief 回收商店业务服务
 */
class RecycleService : public BaseTransactionService {
public:
    static RecycleService& getInstance();

    RecycleService(const RecycleService&)            = delete;
    RecycleService& operator=(const RecycleService&) = delete;

    // === 委托管理 ===
    SetCommissionResult setCommission(
        BlockPos           pos,
        int                dimId,
        const std::string& itemNbt,
        double             price,
        int                minDurability,
        const std::string& requiredEnchants,
        int                maxRecycleCount,
        int                requiredAuxValue = -1
    );

    bool updateCommission(BlockPos pos, int dimId, int itemId, double price, int maxRecycleCount);

    std::vector<RecycleItemData> getCommissions(BlockPos pos, int dimId);

    std::optional<RecycleItemData> getCommission(BlockPos pos, int dimId, int itemId);

    // === 回收记录 ===
    std::vector<RecycleRecordData> getRecycleRecords(BlockPos pos, int dimId, int itemId, int limit = 50);

    // === 回收交易 ===
    // 完整的回收交易流程（包含物品转移、金钱处理、数据库更新）
    RecycleResult executeFullRecycle(
        Player&            recycler,
        BlockPos           pos,
        int                dimId,
        int                itemId,
        int                quantity,
        double             unitPrice,
        const std::string& commissionNbtStr,
        BlockSource&       region
    );

private:
    RecycleService() = default;

    // 内部方法：执行数据库更新（在事务中）
    bool executeDbUpdate(Player& recycler, BlockPos pos, int dimId, int itemId, int quantity, double totalPrice);
};

} // namespace CT