#include "economy.h"
#include "LLMoney.h"
// czmoney/money_api.h 已经在 economy.h 中包含
#include "Config/ConfigManager.h" // 包含配置管理器
#include "config.h"               // 包含 EconomyType 定义
#include "ll/api/service/PlayerInfo.h" // 用于 XUID 到 UUID 的转换

namespace CT {
namespace Economy {

// 辅助函数：通过 XUID 获取 UUID 字符串，如果没有找到则返回空字符串
// 注意: 这个函数可能在某些情况下性能不高，因为它会查询所有在线/离线玩家信息。
// 尽可能直接使用 Player 对象的 getUuid().asString()。
std::string getUuidStringFromXuid(const std::string& xuid) {
    auto playerInfo = ll::service::PlayerInfo::getInstance().fromXuid(xuid);
    if (playerInfo) {
        return playerInfo->uuid.asString();
    }
    return "";
}

bool hasMoney(Player& player, int amount) {
    if (ConfigManager::getInstance().get().economyType == EconomyType::LLMoney) {
        return LLMoney_Get(player.getXuid()) >= amount;
    } else { // CzMoney
        auto balance = czmoney::api::getRawPlayerBalance(player.getUuid().asString(), "money");
        if (balance.has_value()) {
            // czmoney::api::getRawPlayerBalance 返回的是实际金额 * 100，需要除以 100
            return (balance.value() / 100) >= amount;
        }
        return false;
    }
}

bool reduceMoney(Player& player, int amount) {
    if (ConfigManager::getInstance().get().economyType == EconomyType::LLMoney) {
        return LLMoney_Reduce(player.getXuid(), amount);
    } else { // CzMoney
        // czmoney::api::subtractPlayerBalance 接收 double 类型的金额
        auto result = czmoney::api::subtractPlayerBalance(player.getUuid().asString(), "money", static_cast<double>(amount));
        return result == czmoney::api::MoneyApiResult::Success;
    }
}

bool addMoney(Player& player, int amount) {
    if (ConfigManager::getInstance().get().economyType == EconomyType::LLMoney) {
        return LLMoney_Add(player.getXuid(), amount);
    } else { // CzMoney
        // czmoney::api::addPlayerBalance 接收 double 类型的金额
        auto result = czmoney::api::addPlayerBalance(player.getUuid().asString(), "money", static_cast<double>(amount));
        return result == czmoney::api::MoneyApiResult::Success;
    }
}

bool addMoneyByXuid(const std::string& xuid, int amount) {
    if (ConfigManager::getInstance().get().economyType == EconomyType::LLMoney) {
        return LLMoney_Add(xuid, amount);
    } else { // CzMoney
        std::string uuid = getUuidStringFromXuid(xuid);
        if (!uuid.empty()) {
            auto result = czmoney::api::addPlayerBalance(uuid, "money", static_cast<double>(amount));
            return result == czmoney::api::MoneyApiResult::Success;
        }
        return false; // 无法从 XUID 获取 UUID
    }
}

int getMoney(Player& player) {
    return getMoney(player.getXuid()); // 调用 XUID 版本
}

// 通过 XUID 获取玩家金币数量
int getMoney(const std::string& xuid) {
    if (ConfigManager::getInstance().get().economyType == EconomyType::LLMoney) {
        return LLMoney_Get(xuid);
    } else { // CzMoney
        std::string uuid = getUuidStringFromXuid(xuid);
        if (!uuid.empty()) {
            auto balance = czmoney::api::getRawPlayerBalance(uuid, "money");
            if (balance.has_value()) {
                return static_cast<int>(balance.value() / 100);
            }
        }
        return 0; // 无法从 XUID 获取 UUID 或余额
    }
}

// 通过 XUID 扣除玩家金币
bool reduceMoneyByXuid(const std::string& xuid, int amount) {
    if (ConfigManager::getInstance().get().economyType == EconomyType::LLMoney) {
        return LLMoney_Reduce(xuid, amount);
    } else { // CzMoney
        std::string uuid = getUuidStringFromXuid(xuid);
        if (!uuid.empty()) {
            auto result = czmoney::api::subtractPlayerBalance(uuid, "money", static_cast<double>(amount));
            return result == czmoney::api::MoneyApiResult::Success;
        }
        return false; // 无法从 XUID 获取 UUID
    }
}

} // namespace Economy
} // namespace CT
