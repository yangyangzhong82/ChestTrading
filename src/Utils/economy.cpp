#include "economy.h"
#include "LLMoney.h"
// czmoney/money_api.h 已经在 economy.h 中包含
#include "Config/ConfigManager.h"      // 包含配置管理器
#include "config.h"                    // 包含 EconomyType 定义
#include "ll/api/service/PlayerInfo.h" // 用于 XUID 到 UUID 的转换
#include "logger.h" // 包含 logger

namespace CT {
namespace Economy {

// 辅助函数：通过 XUID 获取 UUID 字符串，如果没有找到则返回空字符串
// 注意: 这个函数可能在某些情况下性能不高，因为它会查询所有在线/离线玩家信息。
// 尽可能直接使用 Player 对象的 getUuid().asString()。
std::string getUuidStringFromXuid(const std::string& xuid) {
    auto playerInfo = ll::service::PlayerInfo::getInstance().fromXuid(xuid);
    if (playerInfo) {
        logger.debug("getUuidStringFromXuid: Found UUID {} for XUID {}", playerInfo->uuid.asString(), xuid);
        return playerInfo->uuid.asString();
    }
    logger.warn("getUuidStringFromXuid: Failed to find UUID for XUID {}", xuid);
    return "";
}

bool hasMoney(Player& player, double amount) {
    if (ConfigManager::getInstance().get().economyType == EconomyType::LLMoney) {
        return LLMoney_Get(player.getXuid()) >= static_cast<int>(amount);
    } else { // CzMoney
        auto balance = czmoney::api::getRawPlayerBalance(player.getUuid().asString(), "money");
        if (balance.has_value()) {
            logger.debug("hasMoney: Player {} (UUID: {}) balance: {}", player.getRealName(), player.getUuid().asString(), balance.value());
            return (balance.value()) >= amount; // CzMoney API的getRawPlayerBalance直接返回double
        }
        logger.warn("hasMoney: Failed to get balance for player {} (UUID: {})", player.getRealName(), player.getUuid().asString());
        return false;
    }
}

bool reduceMoney(Player& player, double amount) {
    if (ConfigManager::getInstance().get().economyType == EconomyType::LLMoney) {
        return LLMoney_Reduce(player.getXuid(), static_cast<int>(amount));
    } else { // CzMoney
        auto result = czmoney::api::subtractPlayerBalance(player.getUuid().asString(), "money", amount);
        return result == czmoney::api::MoneyApiResult::Success;
    }
}

bool addMoney(Player& player, double amount) {
    if (ConfigManager::getInstance().get().economyType == EconomyType::LLMoney) {
        return LLMoney_Add(player.getXuid(), static_cast<int>(amount));
    } else { // CzMoney
        auto result = czmoney::api::addPlayerBalance(player.getUuid().asString(), "money", amount);
        return result == czmoney::api::MoneyApiResult::Success;
    }
}

bool addMoneyByXuid(const std::string& xuid, double amount) {
    if (ConfigManager::getInstance().get().economyType == EconomyType::LLMoney) {
        return LLMoney_Add(xuid, static_cast<int>(amount));
    } else { // CzMoney
        std::string uuid = getUuidStringFromXuid(xuid);
        if (!uuid.empty()) {
            auto result = czmoney::api::addPlayerBalance(uuid, "money", amount);
            return result == czmoney::api::MoneyApiResult::Success;
        }
        return false; // 无法从 XUID 获取 UUID
    }
}

double getMoney(Player& player) {
    return getMoney(player.getXuid()); // 调用 XUID 版本
}

// 通过 XUID 获取玩家金币数量
double getMoney(const std::string& xuid) {
    logger.debug("getMoney: Attempting to get money for XUID {}", xuid);
    if (ConfigManager::getInstance().get().economyType == EconomyType::LLMoney) {
        return static_cast<double>(LLMoney_Get(xuid));
    } else { // CzMoney
        std::string uuid = getUuidStringFromXuid(xuid);
        if (!uuid.empty()) {
            auto balance = czmoney::api::getRawPlayerBalance(uuid, "money");
            if (balance.has_value()) {
                logger.debug("getMoney (CzMoney): Balance for UUID {} is {}", uuid, balance.value());
                return balance.value();
            } else {
                logger.warn("getMoney (CzMoney): Failed to get balance for UUID {}", uuid);
            }
        } else {
            logger.warn("getMoney (CzMoney): Failed to get UUID for XUID {}. Returning 0.0.", xuid);
        }
        return 0.0; // 无法从 XUID 获取 UUID 或余额
    }
}

// 通过 XUID 扣除玩家金币
bool reduceMoneyByXuid(const std::string& xuid, double amount) {
    logger.debug("reduceMoneyByXuid: Attempting to reduce money for XUID {} by amount {}", xuid, amount);
    if (ConfigManager::getInstance().get().economyType == EconomyType::LLMoney) {
        // 对于 LLMoney，我们假设它只处理整数。
        // 如果 amount > 0 但小于 1，将其视为 1 以确保扣除。
        // 如果 amount >= 1，则正常转换为整数。
        int amountToReduce = static_cast<int>(amount);
        if (amount > 0 && amountToReduce == 0) {
            amountToReduce = 1; // 向上取整，确保扣除至少1个单位
        }
        bool success = LLMoney_Reduce(xuid, amountToReduce);
        if (success) {
            logger.debug("reduceMoneyByXuid (LLMoney): Successfully reduced {} for XUID {}", amountToReduce, xuid);
        } else {
            logger.warn("reduceMoneyByXuid (LLMoney): Failed to reduce {} for XUID {}", amountToReduce, xuid);
        }
        return success;
    } else { // CzMoney
        std::string uuid = getUuidStringFromXuid(xuid);
        if (!uuid.empty()) {
            auto result = czmoney::api::subtractPlayerBalance(uuid, "money", amount);
            if (result == czmoney::api::MoneyApiResult::Success) {
                logger.debug("reduceMoneyByXuid (CzMoney): Successfully reduced {} for UUID {}", amount, uuid);
                return true;
            } else {
                logger.warn("reduceMoneyByXuid (CzMoney): Failed to reduce {} for UUID {}. Result code: {}", amount, uuid, static_cast<int>(result));
                return false;
            }
        }
        logger.warn("reduceMoneyByXuid (CzMoney): Failed to get UUID for XUID {}. Cannot reduce money.", xuid);
        return false; // 无法从 XUID 获取 UUID
    }
}

// 通过 UUID 增加玩家金币
bool addMoneyByUuid(const std::string& uuid, double amount) {
    if (ConfigManager::getInstance().get().economyType == EconomyType::LLMoney) {
        // LLMoney 使用 XUID，需要从 UUID 转换
        auto playerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(uuid));
        if (playerInfo) {
            return LLMoney_Add(playerInfo->xuid, static_cast<int>(amount));
        }
        return false; // 找不到玩家
    } else { // CzMoney
        auto result = czmoney::api::addPlayerBalance(uuid, "money", amount);
        return result == czmoney::api::MoneyApiResult::Success;
    }
}

// 通过 UUID 扣除玩家金币
bool reduceMoneyByUuid(const std::string& uuid, double amount) {
    if (ConfigManager::getInstance().get().economyType == EconomyType::LLMoney) {
        // LLMoney 使用 XUID，需要从 UUID 转换
        auto playerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(uuid));
        if (playerInfo) {
            return LLMoney_Reduce(playerInfo->xuid, static_cast<int>(amount));
        }
        return false; // 找不到玩家
    } else { // CzMoney
        auto result = czmoney::api::subtractPlayerBalance(uuid, "money", amount);
        return result == czmoney::api::MoneyApiResult::Success;
    }
}

} // namespace Economy
} // namespace CT
