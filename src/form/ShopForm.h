#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include <cstddef>
#include <functional>
#include <string>

namespace CT {

void showShopChestItemsUi(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    const std::string& searchKeyword = "",
    std::size_t        page          = 0
);
void showShopChestItemsForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    const std::string& searchKeyword = ""
);
void showShopItemPriceForm(
    Player&            player,
    const std::string& itemNbtStr,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region
);
void showShopItemManageForm(
    Player&            player,
    const std::string& itemNbtStr,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region
);
void showShopChestManageForm(Player& player, BlockPos pos, int dimId, BlockSource& region);
void showShopItemBuyForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    int                slot,
    double             unitPrice,
    BlockSource&       region,
    const std::string& itemNbtStr,
    const std::string& searchKeyword = "",
    bool               returnToChestUi = false,
    std::size_t        returnPage      = 0
);
void showPlayerPurchaseHistoryForm(Player& player, std::function<void(Player&)> onBack = {});

} // namespace CT
