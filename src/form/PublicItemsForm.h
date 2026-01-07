#pragma once

#include "mc/world/actor/player/Player.h"
#include <string>

namespace CT {

// 显示公开商店物品列表（所有公开商店的具体物品）
void showPublicItemsForm(Player& player, int currentPage = 0, const std::string& searchKeyword = "");

// 显示公开回收商店物品列表
void showPublicRecycleItemsForm(Player& player, int currentPage = 0, const std::string& searchKeyword = "");

} // namespace CT
