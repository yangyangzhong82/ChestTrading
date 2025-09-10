#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include <string>
#include <utility> // For std::pair
#include <vector>  // For std::vector

namespace CT {


// 检查箱子是否被锁定，并返回箱子主人的UUID（如果被锁定）。同时检查玩家是否在分享列表中。
std::pair<bool, std::string> isChestLocked(BlockPos pos, int dimId);

// 上锁箱子
bool lockChest(const std::string& player_uuid, BlockPos pos, int dimId, BlockSource& region);

// 解锁箱子
bool unlockChest(BlockPos pos, int dimId, BlockSource& region);

// 添加分享玩家
bool addSharedPlayer(const std::string& owner_uuid, const std::string& shared_player_uuid, BlockPos pos, int dimId);

// 移除分享玩家
bool removeSharedPlayer(const std::string& shared_player_uuid, BlockPos pos, int dimId);

// 获取所有分享玩家
std::vector<std::string> getSharedPlayers(BlockPos pos, int dimId);

} // namespace CT
