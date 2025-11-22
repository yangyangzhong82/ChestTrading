#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h" 
#include <string>                      
#include "shareForm.h"                  
#include "interaction/chestprotect.h"   

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

}
