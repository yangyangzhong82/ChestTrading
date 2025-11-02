#include "economy.h"
#include "LLMoney.h" // 引入 LLMoney 库

namespace CT {
namespace Economy {

bool hasMoney(Player& player, int amount) {
    return LLMoney_Get(player.getXuid()) >= amount;
}

bool reduceMoney(Player& player, int amount) { return LLMoney_Reduce(player.getXuid(), amount); }

bool addMoney(Player& player, int amount) { return LLMoney_Add(player.getXuid(), amount); }

int getMoney(Player& player) { return LLMoney_Get(player.getXuid()); }

} // namespace Economy
} // namespace CT
