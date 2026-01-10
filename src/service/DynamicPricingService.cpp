#include "DynamicPricingService.h"
#include "logger.h"
#include "repository/DynamicPricingRepository.h"
#include <algorithm>
#include <ctime>

namespace CT {

DynamicPricingService& DynamicPricingService::getInstance() {
    static DynamicPricingService instance;
    return instance;
}

bool DynamicPricingService::checkAndResetIfNeeded(DynamicPricingData& data) {
    int64_t now         = std::time(nullptr);
    int64_t elapsed     = now - data.lastResetTime;
    int64_t intervalSec = static_cast<int64_t>(data.resetIntervalHours) * 3600;

    if (elapsed >= intervalSec) {
        data.currentCount  = 0;
        data.lastResetTime = now;
        DynamicPricingRepository::getInstance().upsert(data);
        return true;
    }
    return false;
}

double DynamicPricingService::calculatePrice(const DynamicPricingData& data) {
    if (data.priceTiers.empty()) return 0.0;

    // priceTiers 已在反序列化时按阈值降序排序
    for (const auto& tier : data.priceTiers) {
        if (data.currentCount >= tier.threshold) {
            return tier.price;
        }
    }

    // 返回最低阈值的价格
    return data.priceTiers.back().price;
}

std::optional<DynamicPriceInfo> DynamicPricingService::getPriceInfo(BlockPos pos, int dimId, int itemId, bool isShop) {
    auto dataOpt = DynamicPricingRepository::getInstance().find(pos, dimId, itemId, isShop);
    if (!dataOpt || !dataOpt->enabled) return std::nullopt;

    auto& data = *dataOpt;
    checkAndResetIfNeeded(data);

    DynamicPriceInfo info;
    info.currentPrice  = calculatePrice(data);
    info.currentCount  = data.currentCount;
    info.stopThreshold = data.stopThreshold;

    // 计算剩余可交易数量
    if (data.stopThreshold > 0) {
        info.remainingQuantity = std::max(0, data.stopThreshold - data.currentCount);
        info.canTrade          = info.remainingQuantity > 0;
    } else {
        info.remainingQuantity = -1;
        info.canTrade          = true;
    }

    // 计算距离重置的小时数
    int64_t now          = std::time(nullptr);
    int64_t elapsed      = now - data.lastResetTime;
    int64_t intervalSec  = static_cast<int64_t>(data.resetIntervalHours) * 3600;
    int64_t remaining    = intervalSec - elapsed;
    info.hoursUntilReset = static_cast<int>((remaining + 3599) / 3600); // 向上取整

    return info;
}

bool DynamicPricingService::canTrade(BlockPos pos, int dimId, int itemId, bool isShop, int quantity) {
    auto infoOpt = getPriceInfo(pos, dimId, itemId, isShop);
    if (!infoOpt) return true; // 无动态价格配置，允许交易

    if (!infoOpt->canTrade) return false;
    if (infoOpt->remainingQuantity == -1) return true;
    return quantity <= infoOpt->remainingQuantity;
}

bool DynamicPricingService::recordTrade(BlockPos pos, int dimId, int itemId, bool isShop, int quantity) {
    bool result = DynamicPricingRepository::getInstance().incrementCount(pos, dimId, itemId, isShop, quantity);
    if (!result) {
        logger.error("记录动态价格交易失败: itemId={}, isShop={}, quantity={}", itemId, isShop, quantity);
    }
    return result;
}

bool DynamicPricingService::setDynamicPricing(
    BlockPos                      pos,
    int                           dimId,
    int                           itemId,
    bool                          isShop,
    const std::vector<PriceTier>& tiers,
    int                           stopThreshold,
    int                           resetHours,
    bool                          enabled
) {
    // 查询现有配置，保留计数和重置时间
    auto existingOpt = DynamicPricingRepository::getInstance().find(pos, dimId, itemId, isShop);

    DynamicPricingData data;
    data.dimId      = dimId;
    data.pos        = pos;
    data.itemId     = itemId;
    data.isShop     = isShop;
    data.priceTiers = tiers;
    // 按阈值降序排序，保持与 deserializeTiers 一致
    std::sort(data.priceTiers.begin(), data.priceTiers.end(), [](const PriceTier& a, const PriceTier& b) {
        return a.threshold > b.threshold;
    });
    data.stopThreshold      = stopThreshold;
    data.resetIntervalHours = resetHours;
    data.enabled            = enabled;

    if (existingOpt) {
        // 保留现有计数和重置时间
        data.currentCount  = existingOpt->currentCount;
        data.lastResetTime = existingOpt->lastResetTime;
    } else {
        data.currentCount  = 0;
        data.lastResetTime = std::time(nullptr);
    }

    bool result = DynamicPricingRepository::getInstance().upsert(data);
    if (!result) {
        logger.error("设置动态价格失败: itemId={}, isShop={}", itemId, isShop);
    }
    return result;
}

bool DynamicPricingService::removeDynamicPricing(BlockPos pos, int dimId, int itemId, bool isShop) {
    bool result = DynamicPricingRepository::getInstance().remove(pos, dimId, itemId, isShop);
    if (!result) {
        logger.error("删除动态价格失败: itemId={}, isShop={}", itemId, isShop);
    }
    return result;
}

std::optional<DynamicPricingData>
DynamicPricingService::getDynamicPricing(BlockPos pos, int dimId, int itemId, bool isShop) {
    auto dataOpt = DynamicPricingRepository::getInstance().find(pos, dimId, itemId, isShop);
    if (dataOpt) {
        checkAndResetIfNeeded(*dataOpt);
    }
    return dataOpt;
}

void DynamicPricingService::checkAndResetCounters() { DynamicPricingRepository::getInstance().resetExpiredCounters(); }

} // namespace CT
