#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "repository/ChestRepository.h"

namespace CT {

// 显示公开商店列表
void showPublicShopListForm(
    Player&            player,
    int                currentPage   = 0,
    const std::string& searchKeyword = "",
    const std::string& searchType    = "owner"
);

// 显示公开回收商店列表
void showPublicRecycleShopListForm(
    Player&            player,
    int                currentPage   = 0,
    const std::string& searchKeyword = "",
    const std::string& searchType    = "owner"
);

// 显示商店预览表单（只能预览，不能购买，可传送）
void showShopPreviewForm(Player& player, const ChestData& shop);

// 显示回收商店预览表单（只能预览，不能回收，可传送）
void showRecycleShopPreviewForm(Player& player, const ChestData& shop);

} // namespace CT
