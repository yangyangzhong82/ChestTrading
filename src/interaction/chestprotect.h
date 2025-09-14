#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include <string>
#include <utility> // For std::pair
#include <vector>  // For std::vector
#include <tuple>

namespace CT {

enum class ChestType {
    Locked      = 0, // 普通锁
    RecycleShop = 1, // 回收商店
    Shop        = 2  // 商店
};


// 检查箱子状态，返回是否锁定、主人UUID和箱子类型
std::tuple<bool, std::string, ChestType> getChestDetails(BlockPos pos, int dimId);

// 设置箱子（上锁、设置商店等）
bool setChest(const std::string& player_uuid, BlockPos pos, int dimId, BlockSource& region, ChestType type);

// 移除箱子设置（解锁）
bool removeChest(BlockPos pos, int dimId, BlockSource& region);

// 添加分享玩家
bool addSharedPlayer(const std::string& owner_uuid, const std::string& shared_player_uuid, BlockPos pos, int dimId);

// 移除分享玩家
bool removeSharedPlayer(const std::string& shared_player_uuid, BlockPos pos, int dimId);

// 获取所有分享玩家
std::vector<std::string> getSharedPlayers(BlockPos pos, int dimId);

} // namespace CT
