#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include <string>
#include <vector>

namespace CT {

const int ITEMS_PER_PAGE = 7; // 每页显示的玩家数量
const std::string OFFLINE_PLAYER_INPUT_KEY = "offline_player_name"; // 离线玩家输入框的键名

void showShareForm(Player& player, BlockPos pos, int dimId, const std::string& ownerUuid, BlockSource& region, int currentPage = 0);
void showAddOfflineShareForm(Player& player, BlockPos pos, int dimId, const std::string& ownerUuid, BlockSource& region);
void showAddShareForm(Player& player, BlockPos pos, int dimId, const std::string& ownerUuid, BlockSource& region, int currentPage = 0);
void showRemoveShareForm(Player& player, BlockPos pos, int dimId, const std::string& ownerUuid, BlockSource& region, int currentPage = 0);

}
