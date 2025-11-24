#include "economy.h"
#include "LLMoney.h" 
#include "czmoney/money_api.h"

namespace CT {
namespace Economy {

bool hasMoney(Player& player, int amount) {
    return LLMoney_Get(player.getXuid()) >= amount;
}

bool reduceMoney(Player& player, int amount) { return LLMoney_Reduce(player.getXuid(), amount); }

bool addMoney(Player& player, int amount) { return LLMoney_Add(player.getXuid(), amount); }

bool addMoneyByXuid(const std::string& xuid, int amount) { return LLMoney_Add(xuid, amount); }

int getMoney(Player& player) { return LLMoney_Get(player.getXuid()); }

} // namespace Economy
} // namespace CT
