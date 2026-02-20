#pragma once

#include "mc/world/level/BlockPos.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace CT {

// 玩家限购配置
struct PlayerLimitConfig {
    int         dimId;
    BlockPos    pos;
    std::string playerUuid; // 空字符串表示全局限制
    int         limitCount;
    int         limitSeconds; // 限制时间窗口（秒）
    bool        isShop;       // true=商店购买限制, false=回收限制
};

// 玩家交易记录（用于计算限购）
struct PlayerTradeRecord {
    int         dimId;
    BlockPos    pos;
    std::string playerUuid;
    int         tradeCount;
    std::string timestamp;
    bool        isShop;
};

class PlayerLimitRepository {
public:
    static PlayerLimitRepository& getInstance();

    PlayerLimitRepository(const PlayerLimitRepository&)            = delete;
    PlayerLimitRepository& operator=(const PlayerLimitRepository&) = delete;

    // === 限购配置 ===
    bool                             upsertLimit(const PlayerLimitConfig& config);
    bool                             removeLimit(BlockPos pos, int dimId, const std::string& playerUuid, bool isShop);
    bool                             removeAllLimits(BlockPos pos, int dimId);
    std::optional<PlayerLimitConfig> getLimit(BlockPos pos, int dimId, const std::string& playerUuid, bool isShop);
    std::vector<PlayerLimitConfig>   getAllLimits(BlockPos pos, int dimId, bool isShop);

    // === 交易记录查询 ===
    int getTradeCountInWindow(BlockPos pos, int dimId, const std::string& playerUuid, int windowSeconds, bool isShop);
    bool upsertLimitResetPoint(BlockPos pos, int dimId, bool isShop, int64_t resetTime);

private:
    PlayerLimitRepository() = default;
};

} // namespace CT
