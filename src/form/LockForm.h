#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h" // 引入 BlockSource
#include <string>                       // 引入 string
#include "shareForm.h"                  // 引入 shareForm
#include "interaction/chestprotect.h"   // 引入 chestprotect 以使用 ChestType

namespace CT {

void showChestLockForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    bool               isLocked,
    const std::string& ownerUuid,
    ChestType          chestType,
    BlockSource&       region
);

void showShopChestItemsForm(Player& player, BlockPos pos, int dimId, BlockSource& region);
void showItemDetailsForm(Player& player, const ItemStack& item, BlockPos pos, int dimId, BlockSource& region);

}
