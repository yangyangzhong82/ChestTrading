#pragma once

#include "mc/world/actor/player/Player.h"
#include <chrono>
#include <map>
#include <shared_mutex>
#include <string>

namespace CT {

class TeleportService {
public:
    static TeleportService& getInstance();

    /**
     * @brief 检查玩家是否可以传送（冷却时间是否已过）
     * @param playerUuid 玩家UUID
     * @return 如果可以传送返回true，否则返回false
     */
    bool canTeleport(const std::string& playerUuid);

    /**
     * @brief 获取玩家剩余冷却时间（秒）
     * @param playerUuid 玩家UUID
     * @return 剩余冷却秒数，如果可以传送则返回0
     */
    int getRemainingCooldown(const std::string& playerUuid);

    /**
     * @brief 记录玩家传送时间，开始冷却
     * @param playerUuid 玩家UUID
     */
    void recordTeleport(const std::string& playerUuid);

    /**
     * @brief 清理过期的冷却记录
     */
    void cleanupExpiredCooldowns();

private:
    TeleportService()  = default;
    ~TeleportService() = default;

    TeleportService(const TeleportService&)            = delete;
    TeleportService& operator=(const TeleportService&) = delete;

    // 存储玩家UUID和最后传送时间的映射
    std::map<std::string, std::chrono::steady_clock::time_point> mTeleportCooldowns;
    mutable std::shared_mutex                                    mCooldownMutex;
};

} // namespace CT