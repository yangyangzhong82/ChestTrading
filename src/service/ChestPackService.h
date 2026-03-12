#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

namespace CT {

bool packChestForPlayer(Player& player, BlockPos pos, int dimId, BlockSource& region);

} // namespace CT
