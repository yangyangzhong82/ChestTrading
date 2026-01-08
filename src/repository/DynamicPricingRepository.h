#pragma once

#include "mc/world/level/BlockPos.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace CT {

// 价格阶梯
struct PriceTier {
    int    threshold; // 交易量阈值
    double price;     // 该阶梯的价格
};

// 动态价格配置
struct DynamicPricingData {
    int                    dimId;
    BlockPos               pos;
    int                    itemId;
    bool                   isShop; // true=商店, false=回收
    std::vector<PriceTier> priceTiers;
    int                    stopThreshold;      // 停止交易的阈值，-1表示不限制
    int                    currentCount;       // 当前周期已交易数量
    int                    resetIntervalHours; // 重置周期（小时）
    int64_t                lastResetTime;      // 上次重置时间戳
    bool                   enabled = true;     // 是否启用动态价格
};

class DynamicPricingRepository {
public:
    static DynamicPricingRepository& getInstance();

    DynamicPricingRepository(const DynamicPricingRepository&)            = delete;
    DynamicPricingRepository& operator=(const DynamicPricingRepository&) = delete;

    bool                              upsert(const DynamicPricingData& data);
    bool                              remove(BlockPos pos, int dimId, int itemId, bool isShop);
    bool                              removeAll(BlockPos pos, int dimId);
    std::optional<DynamicPricingData> find(BlockPos pos, int dimId, int itemId, bool isShop);
    std::vector<DynamicPricingData>   findAll(BlockPos pos, int dimId);

    bool incrementCount(BlockPos pos, int dimId, int itemId, bool isShop, int amount);
    int  resetExpiredCounters(); // 返回重置的记录数

private:
    DynamicPricingRepository() = default;

    std::string            serializeTiers(const std::vector<PriceTier>& tiers);
    std::vector<PriceTier> deserializeTiers(const std::string& json);
};

} // namespace CT
