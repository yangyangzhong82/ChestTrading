#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include <functional>
#include <string>

namespace CT {

void showShopChestItemsForm(Player& player, BlockPos pos, int dimId, BlockSource& region);
void showShopItemPriceForm(Player& player, const ItemStack& item, BlockPos pos, int dimId, BlockSource& region);
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
    const ItemStack&   item,
    BlockPos           pos,
    int                dimId,
    int                slot,
    double             unitPrice,
    BlockSource&       region,
    const std::string& itemNbtStr
);
void showPlayerPurchaseHistoryForm(Player& player, std::function<void(Player&)> onBack = {});

} // namespace CT
