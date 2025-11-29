#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include <string>
#include <vector>

namespace CT {

void showRecycleForm(Player& player, BlockPos pos, int dimId, BlockSource& region);
void showRecycleItemListForm(Player& player, BlockPos pos, int dimId, BlockSource& region);
void showRecycleConfirmForm(
    Player&            player,
    const ItemStack&   item,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    int                slotIndex,
    double             unitPrice, 
    const std::string& commissionNbtStr = ""
);
void showRecycleShopManageForm(Player& player, BlockPos pos, int dimId, BlockSource& region);

} // namespace CT
