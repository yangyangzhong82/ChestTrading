#pragma once

#include "Types.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"

namespace ll::form {
class CustomForm;
}
class BlockSource;
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace CT {

// 用于 GetAllChests 的返回结构
struct ChestInfo {
    int         dimId;
    BlockPos    pos;
    std::string ownerUuid;
    ChestType   type;
    std::string shopName;
    bool        enableFloatingText = true;
    bool        enableFakeItem     = true;
    bool        isPublic           = true;
};

// 检查箱子状态，返回是否锁定、主人UUID和箱子类型
std::tuple<bool, std::string, ChestType> getChestDetails(BlockPos pos, int dimId, BlockSource& region);

// 设置箱子（上锁、设置商店等）
bool setChest(const std::string& player_uuid, BlockPos pos, int dimId, BlockSource& region, ChestType type);

// 移除箱子设置（解锁）
bool removeChest(BlockPos pos, int dimId, BlockSource& region);

// 添加分享玩家
bool addSharedPlayer(
    const std::string& owner_uuid,
    const std::string& shared_player_uuid,
    BlockPos           pos,
    int                dimId,
    BlockSource*       region = nullptr
);

// 移除分享玩家
bool removeSharedPlayer(const std::string& shared_player_uuid, BlockPos pos, int dimId, BlockSource* region = nullptr);

// 获取所有分享玩家（包含主人信息）
std::vector<std::pair<std::string, std::string>>
getSharedPlayersWithOwner(BlockPos pos, int dimId, BlockSource& region);

// 获取所有分享玩家（仅玩家UUID）
std::vector<std::string> getSharedPlayers(BlockPos pos, int dimId, BlockSource& region);

// 检查玩家是否有权打开箱子
bool canPlayerOpenChest(const std::string& player_uuid, BlockPos pos, int dimId, BlockSource& region);

namespace internal {
// 获取箱子的主方块位置，对于双联箱，这总是一个确定的方块
BlockPos GetMainChestPos(BlockPos pos, BlockSource& region);
} // namespace internal

// 获取所有箱子信息
std::vector<ChestInfo> getAllChests();

// 获取商店名称
std::string getShopName(BlockPos pos, int dimId, BlockSource& region);

// 设置商店名称
bool setShopName(BlockPos pos, int dimId, BlockSource& region, const std::string& shopName);

// 获取箱子配置
struct ChestConfig {
    bool enableFloatingText = true;
    bool enableFakeItem     = true;
    bool isPublic           = true;
};
ChestConfig getChestConfig(BlockPos pos, int dimId, BlockSource& region);

// 设置箱子配置
bool setChestConfig(BlockPos pos, int dimId, BlockSource& region, const ChestConfig& config);

// 获取玩家拥有的特定类型箱子数量
int getPlayerChestCount(const std::string& playerUuid, ChestType type);

// 检查玩家是否可以创建特定类型的箱子（检查数量限制和权限）
bool canPlayerCreateChest(const std::string& playerUuid, ChestType type, std::string& errorMessage);

} // namespace CT
