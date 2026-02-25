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
PlayerLimitService::checkLimit(
    BlockPos           pos,
    int                dimId,
    const std::string& playerUuid,
    int                quantity,
    bool               isShop,
    int                itemId
) {
    auto& repo = PlayerLimitRepository::getInstance();
    auto& txt  = TextService::getInstance();

    // 优先检查商品级限购；不存在时回退到整箱限购(itemId=0)
    std::optional<PlayerLimitConfig> appliedLimit;
    int                              tradeCountItemId = -1; // -1=整箱统计所有商品
    if (itemId > 0) {
        appliedLimit = repo.getLimit(pos, dimId, "", isShop, itemId);
        if (appliedLimit) {
            tradeCountItemId = itemId;
        }
    }
    if (!appliedLimit) {
        appliedLimit = repo.getLimit(pos, dimId, "", isShop, 0);
    }

    if (!appliedLimit) {
        return {true, "", -1}; // 无限制
    }

    // 根据玩家的交易记录计算剩余配额
    int tradedCount = repo.getTradeCountInWindow(
        pos,
        dimId,
        playerUuid,
        appliedLimit->limitSeconds,
        isShop,
        tradeCountItemId
    );
    int remaining = appliedLimit->limitCount - tradedCount;

    if (remaining <= 0) {
        std::string typeStr = isShop ? txt.getMessage("limit.type_buy") : txt.getMessage("limit.type_recycle");
        return {
            false,
            txt.getMessage(
                "limit.exceeded",
                {{"type", typeStr},
                  {"limit", std::to_string(appliedLimit->limitCount)},
                  {"seconds", std::to_string(appliedLimit->limitSeconds)}}
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
    bool               isShop,
    int                itemId
) {
    PlayerLimitConfig config{dimId, pos, playerUuid, itemId, limitCount, limitSeconds, isShop};
    bool              result = PlayerLimitRepository::getInstance().upsertLimit(config);
    if (!result) {
        logger.error(
            "设置限购失败: playerUuid={}, itemId={}, limitCount={}, isShop={}",
            playerUuid,
            itemId,
            limitCount,
            isShop
        );
    }
    return result;
}

bool
PlayerLimitService::removeLimit(BlockPos pos, int dimId, const std::string& playerUuid, bool isShop, int itemId) {
    bool result = PlayerLimitRepository::getInstance().removeLimit(pos, dimId, playerUuid, isShop, itemId);
    if (!result) {
        logger.error("删除限购失败: playerUuid={}, itemId={}, isShop={}", playerUuid, itemId, isShop);
    }
    return result;
}

std::optional<PlayerLimitConfig>
PlayerLimitService::getLimit(BlockPos pos, int dimId, const std::string& playerUuid, bool isShop, int itemId) {
    return PlayerLimitRepository::getInstance().getLimit(pos, dimId, playerUuid, isShop, itemId);
}

int PlayerLimitService::getRemainingQuota(
    BlockPos           pos,
    int                dimId,
    const std::string& playerUuid,
    bool               isShop,
    int                itemId
) {
    auto& repo = PlayerLimitRepository::getInstance();

    std::optional<PlayerLimitConfig> appliedLimit;
    int                              tradeCountItemId = -1;
    if (itemId > 0) {
        appliedLimit = repo.getLimit(pos, dimId, "", isShop, itemId);
        if (appliedLimit) {
            tradeCountItemId = itemId;
        }
    }
    if (!appliedLimit) {
        appliedLimit = repo.getLimit(pos, dimId, "", isShop, 0);
    }

    if (!appliedLimit) {
        return -1; // 无限制
    }

    int tradedCount =
        repo.getTradeCountInWindow(pos, dimId, playerUuid, appliedLimit->limitSeconds, isShop, tradeCountItemId);
    return std::max(0, appliedLimit->limitCount - tradedCount);
}

bool PlayerLimitService::resetLimitWindow(BlockPos pos, int dimId, bool isShop, int itemId) {
    int64_t nowTs  = static_cast<int64_t>(std::time(nullptr));
    bool    result = PlayerLimitRepository::getInstance().upsertLimitResetPoint(pos, dimId, isShop, nowTs, itemId);
    if (!result) {
        logger.error(
            "手动重置限购窗口失败: dimId={}, pos=({}, {}, {}), isShop={}, itemId={}",
            dimId,
            pos.x,
            pos.y,
            pos.z,
            isShop,
            itemId
        );
    }
    return result;
}

} // namespace CT
