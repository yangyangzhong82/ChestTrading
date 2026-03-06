#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include <functional>

namespace CT {

void showTradeRecordMenuForm(Player& player, std::function<void(Player&)> onBack = {}, bool showLastPurchase = true);
void showPlayerTradeRecordsForm(Player& player, std::function<void(Player&)> onBack = {});
void showShopTradeRecordsForm(
    Player&                      player,
    BlockPos                     pos,
    int                          dimId,
    std::function<void(Player&)> onBack = {}
);
void showRecycleShopTradeRecordsForm(
    Player&                      player,
    BlockPos                     pos,
    int                          dimId,
    std::function<void(Player&)> onBack = {}
);

} // namespace CT
