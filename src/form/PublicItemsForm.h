#pragma once

#include "mc/world/actor/player/Player.h"
#include <cstddef>
#include <functional>
#include <string>

namespace CT {

// 显示公开商店物品列表（所有公开商店的具体物品）
void showPublicItemsForm(Player& player, int currentPage = 0, const std::string& searchKeyword = "");

// 显示公开商店物品列表（ChestUI 版本）
void showPublicItemsChestUi(Player& player, std::size_t currentPage = 0, const std::string& searchKeyword = "");
void showPublicItemsChestUi(
    Player&            player,
    std::size_t        currentPage,
    const std::string& searchKeyword,
    std::function<void(Player&)> onBack
);
void showPlayerPublicItemsChestUi(
    Player&            player,
    const std::string& ownerUuid,
    std::size_t        currentPage = 0,
    const std::string& searchKeyword = "",
    std::function<void(Player&)> onBack = {}
);

// 显示公开回收商店物品列表
void showPublicRecycleItemsForm(Player& player, int currentPage = 0, const std::string& searchKeyword = "");

// 显示公开回收商店物品列表（ChestUI 版本）
void showPublicRecycleItemsChestUi(Player& player, std::size_t currentPage = 0, const std::string& searchKeyword = "");
void showPublicRecycleItemsChestUi(
    Player&            player,
    std::size_t        currentPage,
    const std::string& searchKeyword,
    std::function<void(Player&)> onBack
);
void showPlayerPublicRecycleItemsChestUi(
    Player&            player,
    const std::string& ownerUuid,
    std::size_t        currentPage = 0,
    const std::string& searchKeyword = "",
    std::function<void(Player&)> onBack = {}
);

} // namespace CT
