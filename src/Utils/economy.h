#pragma once

#include "mc/world/actor/player/Player.h"
#include <string>
#include "czmoney/money_api.h" // 添加 czmoney API 头文件

namespace CT {
namespace Economy {

// 检查玩家是否有足够的金币
bool hasMoney(Player& player, double amount);

// 扣除玩家金币
bool reduceMoney(Player& player, double amount);

// 增加玩家金币
bool addMoney(Player& player, double amount);

// 通过XUID增加玩家金币
bool addMoneyByXuid(const std::string& xuid, double amount);

// 通过UUID增加玩家金币
bool addMoneyByUuid(const std::string& uuid, double amount);

// 获取玩家金币数量
double getMoney(Player& player);

// 通过XUID获取玩家金币数量
double getMoney(const std::string& xuid);

// 通过XUID扣除玩家金币
bool reduceMoneyByXuid(const std::string& xuid, double amount);

// 通过UUID扣除玩家金币
bool reduceMoneyByUuid(const std::string& uuid, double amount);

} // namespace Economy
} // namespace CT
