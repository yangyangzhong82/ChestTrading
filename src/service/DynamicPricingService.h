#pragma once

#include "mc/world/level/BlockPos.h"
#include "repository/DynamicPricingRepository.h"
#include <optional>

namespace CT {

// 动态价格信息
struct DynamicPriceInfo {
    double currentPrice;      // 当前价格
    int    remainingQuantity; // 剩余可交易数量，-1表示无限制
    bool   canTrade;          // 是否可以交易
    int    currentCount;      // 当前已交易数量
    int    stopThreshold;     // 停止阈值
    int    hoursUntilReset;   // 距离重置的小时数
};

class DynamicPricingService {
public:
    static DynamicPricingService& getInstance();

    DynamicPricingService(const DynamicPricingService&)            = delete;
    DynamicPricingService& operator=(const DynamicPricingService&) = delete;

    // 获取动态价格信息（如果没有配置返回 nullopt）
    std::optional<DynamicPriceInfo> getPriceInfo(BlockPos pos, int dimId, int itemId, bool isShop);

    // 检查是否可以交易指定数量
    bool canTrade(BlockPos pos, int dimId, int itemId, bool isShop, int quantity);

    // 记录交易（增加计数）
    bool recordTrade(BlockPos pos, int dimId, int itemId, bool isShop, int quantity);

    // 设置动态价格规则
    bool setDynamicPricing(
        BlockPos                      pos,
        int                           dimId,
        int                           itemId,
        bool                          isShop,
        const std::vector<PriceTier>& tiers,
        int                           stopThreshold,
        int                           resetHours,
        bool                          enabled = true
    );

    // 移除动态价格规则
    bool removeDynamicPricing(BlockPos pos, int dimId, int itemId, bool isShop);

    // 获取动态价格配置
    std::optional<DynamicPricingData> getDynamicPricing(BlockPos pos, int dimId, int itemId, bool isShop);

    // 检查并重置过期计数器（定时调用）
    void checkAndResetCounters();

private:
    DynamicPricingService() = default;

    // 检查是否需要重置并执行重置
    bool checkAndResetIfNeeded(DynamicPricingData& data);

    // 根据当前交易量计算价格
    double calculatePrice(const DynamicPricingData& data);
};

} // namespace CT
