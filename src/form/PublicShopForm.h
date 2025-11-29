#pragma once

#include "mc/world/actor/player/Player.h"

namespace CT {

// 显示公开商店列表
void showPublicShopListForm(Player& player, int currentPage = 0, const std::string& searchKeyword = "", const std::string& searchType = "owner");

// 显示公开回收商店列表
void showPublicRecycleShopListForm(Player& player, int currentPage = 0, const std::string& searchKeyword = "", const std::string& searchType = "owner");

} // namespace CT
