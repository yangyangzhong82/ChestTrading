#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include <string>
#include <utility> // For std::pair

namespace CT {


// 检查箱子是否被锁定，并返回箱子主人的UUID（如果被锁定）
std::pair<bool, std::string> isChestLocked(BlockPos pos, int dimId);

// 上锁箱子
bool lockChest(const std::string& player_uuid, BlockPos pos, int dimId);

} // namespace CT
