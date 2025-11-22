#pragma once

#include "mc/world/actor/player/Player.h"
#include <string>

namespace CT {
namespace Economy {

// 检查玩家是否有足够的金币
bool hasMoney(Player& player, int amount);

// 扣除玩家金币
bool reduceMoney(Player& player, int amount);

// 增加玩家金币
bool addMoney(Player& player, int amount);

// 通过XUID增加玩家金币
bool addMoneyByXuid(const std::string& xuid, int amount);

// 获取玩家金币数量
int getMoney(Player& player);

} // namespace Economy
} // namespace CT
