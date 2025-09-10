#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h" // 引入 BlockSource
#include <string> // 引入 string
#include "shareForm.h" // 引入 shareForm

namespace CT {

void showChestLockForm(Player& player, BlockPos pos, int dimId, bool isLocked, const std::string& ownerUuid, BlockSource& region);
}
