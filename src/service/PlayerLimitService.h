#pragma once

#include "mc/world/level/BlockPos.h"
#include "repository/PlayerLimitRepository.h"
#include <optional>
#include <string>

namespace CT {

struct LimitCheckResult {
    bool        allowed;
    std::string message;
    int         remaining; // 剩余可交易数量，-1表示无限制
};

class PlayerLimitService {
public:
    static PlayerLimitService& getInstance();

    PlayerLimitService(const PlayerLimitService&)            = delete;
    PlayerLimitService& operator=(const PlayerLimitService&) = delete;

    // 检查玩家是否可以交易指定数量
    LimitCheckResult checkLimit(BlockPos pos, int dimId, const std::string& playerUuid, int quantity, bool isShop);

    // 设置全局限购配置（playerUuid应为空字符串）
    bool
    setLimit(BlockPos pos, int dimId, const std::string& playerUuid, int limitCount, int limitSeconds, bool isShop);

    // 移除全局限购配置
    bool removeLimit(BlockPos pos, int dimId, const std::string& playerUuid, bool isShop);

    // 获取全局限购配置
    std::optional<PlayerLimitConfig> getLimit(BlockPos pos, int dimId, const std::string& playerUuid, bool isShop);

    // 获取玩家剩余可交易数量
    int getRemainingQuota(BlockPos pos, int dimId, const std::string& playerUuid, bool isShop);

private:
    PlayerLimitService() = default;
};

} // namespace CT
