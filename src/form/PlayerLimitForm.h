#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

namespace CT {

// 显示限购管理表单
void showPlayerLimitForm(Player& player, BlockPos pos, int dimId, BlockSource& region, bool isShop);

} // namespace CT
