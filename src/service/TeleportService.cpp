#include "TeleportService.h"
#include "Config/ConfigManager.h"
#include "logger.h"

namespace CT {

TeleportService& TeleportService::getInstance() {
    static TeleportService instance;
    return instance;
}

bool TeleportService::canTeleport(const std::string& playerUuid) {
    auto it = mTeleportCooldowns.find(playerUuid);
    if (it == mTeleportCooldowns.end()) {
        return true; // 没有记录，可以传送
    }

    auto now             = std::chrono::steady_clock::now();
    auto timeSinceLastTp = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
    int  cooldownSeconds = ConfigManager::getInstance().get().teleportSettings.teleportCooldownSec;

    return timeSinceLastTp >= cooldownSeconds;
}

int TeleportService::getRemainingCooldown(const std::string& playerUuid) {
    auto it = mTeleportCooldowns.find(playerUuid);
    if (it == mTeleportCooldowns.end()) {
        return 0; // 没有记录，没有冷却
    }

    auto now             = std::chrono::steady_clock::now();
    auto timeSinceLastTp = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
    int  cooldownSeconds = ConfigManager::getInstance().get().teleportSettings.teleportCooldownSec;
    int  remaining       = cooldownSeconds - static_cast<int>(timeSinceLastTp);

    return remaining > 0 ? remaining : 0;
}

void TeleportService::recordTeleport(const std::string& playerUuid) {
    mTeleportCooldowns[playerUuid] = std::chrono::steady_clock::now();
    logger.debug("TeleportService: 记录玩家 {} 的传送时间", playerUuid);
}

void TeleportService::cleanupExpiredCooldowns() {
    auto now             = std::chrono::steady_clock::now();
    int  cooldownSeconds = ConfigManager::getInstance().get().teleportSettings.teleportCooldownSec;

    for (auto it = mTeleportCooldowns.begin(); it != mTeleportCooldowns.end();) {
        auto timeSinceLastTp = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
        if (timeSinceLastTp >= cooldownSeconds) {
            logger.debug("TeleportService: 清理过期的传送冷却记录，玩家UUID: {}", it->first);
            it = mTeleportCooldowns.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace CT