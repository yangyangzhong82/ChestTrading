#include "PlayerLimitService.h"
#include "TextService.h"
#include "logger.h"
#include "repository/PlayerLimitRepository.h"
#include <ctime>

namespace CT {

PlayerLimitService& PlayerLimitService::getInstance() {
    static PlayerLimitService instance;
    return instance;
}

LimitCheckResult
PlayerLimitService::checkLimit(BlockPos pos, int dimId, const std::string& playerUuid, int quantity, bool isShop) {
    auto& repo = PlayerLimitRepository::getInstance();
    auto& txt  = TextService::getInstance();

    // 检查全局限制（playerUuid为空）
    auto globalLimit = repo.getLimit(pos, dimId, "", isShop);

    if (!globalLimit) {
        return {true, "", -1}; // 无限制
    }

    // 根据玩家的交易记录计算剩余配额
    int tradedCount = repo.getTradeCountInWindow(pos, dimId, playerUuid, globalLimit->limitSeconds, isShop);
    int remaining   = globalLimit->limitCount - tradedCount;

    if (remaining <= 0) {
        std::string typeStr = isShop ? txt.getMessage("limit.type_buy") : txt.getMessage("limit.type_recycle");
        return {
            false,
            txt.getMessage(
                "limit.exceeded",
                {{"type", typeStr},
                  {"limit", std::to_string(globalLimit->limitCount)},
                  {"seconds", std::to_string(globalLimit->limitSeconds)}}
            ),
            0
        };
    }

    if (quantity > remaining) {
        std::string typeStr = isShop ? txt.getMessage("limit.type_buy") : txt.getMessage("limit.type_recycle");
        return {
            false,
            txt.getMessage(
                "limit.partial",
                {{"type", typeStr}, {"remaining", std::to_string(remaining)}, {"requested", std::to_string(quantity)}}
            ),
            remaining
        };
    }

    return {true, "", remaining};
}

bool PlayerLimitService::setLimit(
    BlockPos           pos,
    int                dimId,
    const std::string& playerUuid,
    int                limitCount,
    int                limitSeconds,
    bool               isShop
) {
    PlayerLimitConfig config{dimId, pos, playerUuid, limitCount, limitSeconds, isShop};
    bool              result = PlayerLimitRepository::getInstance().upsertLimit(config);
    if (!result) {
        logger.error("设置限购失败: playerUuid={}, limitCount={}, isShop={}", playerUuid, limitCount, isShop);
    }
    return result;
}

bool PlayerLimitService::removeLimit(BlockPos pos, int dimId, const std::string& playerUuid, bool isShop) {
    bool result = PlayerLimitRepository::getInstance().removeLimit(pos, dimId, playerUuid, isShop);
    if (!result) {
        logger.error("删除限购失败: playerUuid={}, isShop={}", playerUuid, isShop);
    }
    return result;
}

std::optional<PlayerLimitConfig>
PlayerLimitService::getLimit(BlockPos pos, int dimId, const std::string& playerUuid, bool isShop) {
    return PlayerLimitRepository::getInstance().getLimit(pos, dimId, playerUuid, isShop);
}

int PlayerLimitService::getRemainingQuota(BlockPos pos, int dimId, const std::string& playerUuid, bool isShop) {
    auto& repo = PlayerLimitRepository::getInstance();

    // 检查全局限制
    auto globalLimit = repo.getLimit(pos, dimId, "", isShop);

    if (!globalLimit) {
        return -1; // 无限制
    }

    int tradedCount = repo.getTradeCountInWindow(pos, dimId, playerUuid, globalLimit->limitSeconds, isShop);
    return std::max(0, globalLimit->limitCount - tradedCount);
}

bool PlayerLimitService::resetLimitWindow(BlockPos pos, int dimId, bool isShop) {
    int64_t nowTs  = static_cast<int64_t>(std::time(nullptr));
    bool    result = PlayerLimitRepository::getInstance().upsertLimitResetPoint(pos, dimId, isShop, nowTs);
    if (!result) {
        logger.error("手动重置限购窗口失败: dimId={}, pos=({}, {}, {}), isShop={}", dimId, pos.x, pos.y, pos.z, isShop);
    }
    return result;
}

} // namespace CT
