#pragma once

#include "Types.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "shareForm.h"
#include <string>


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

void showRecycleShopManageForm(Player& player, BlockPos pos, int dimId, BlockSource& region);

} // namespace CT
