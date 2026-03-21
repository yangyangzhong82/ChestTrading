#pragma once

#include <functional>
#include <cstddef>
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "repository/ChestRepository.h"

namespace CT {

// 官方商店筛选选项
enum class OfficialFilter {
    All      = 0, // 全部
    Official = 1, // 仅官方
    Player   = 2  // 仅玩家
};

// 列表排序方式
enum class PublicListSortMode {
    Sales  = 0, // 按销量
    Latest = 1  // 按最新上架
};

// 显示公开商店列表
void showPublicShopListForm(
    Player&            player,
    int                currentPage    = 0,
    const std::string& searchKeyword  = "",
    const std::string& searchType     = "owner",
    OfficialFilter     officialFilter = OfficialFilter::All,
    PublicListSortMode sortMode       = PublicListSortMode::Sales
);

// 显示公开商店列表（ChestUI 版本）
void showPublicShopListChestUi(
    Player&            player,
    std::size_t        currentPage    = 0,
    const std::string& searchKeyword  = "",
    const std::string& searchType     = "owner",
    OfficialFilter     officialFilter = OfficialFilter::All,
    PublicListSortMode sortMode       = PublicListSortMode::Sales,
    std::function<void(Player&)> onBack = {}
);

// 显示公开回收商店列表
void showPublicRecycleShopListForm(
    Player&            player,
    int                currentPage    = 0,
    const std::string& searchKeyword  = "",
    const std::string& searchType     = "owner",
    OfficialFilter     officialFilter = OfficialFilter::All,
    PublicListSortMode sortMode       = PublicListSortMode::Sales
);

// 显示公开回收商店列表（ChestUI 版本）
void showPublicRecycleShopListChestUi(
    Player&            player,
    std::size_t        currentPage    = 0,
    const std::string& searchKeyword  = "",
    const std::string& searchType     = "owner",
    OfficialFilter     officialFilter = OfficialFilter::All,
    PublicListSortMode sortMode       = PublicListSortMode::Sales,
    std::function<void(Player&)> onBack = {}
);

// 显示商店预览表单（只能预览，不能购买，可传送）
void showShopPreviewForm(Player& player, const ChestData& shop, std::function<void(Player&)> onBack = {});

// 显示回收商店预览表单（只能预览，不能回收，可传送）
void showRecycleShopPreviewForm(Player& player, const ChestData& shop, std::function<void(Player&)> onBack = {});

// 显示指定玩家的商店列表
void showPlayerShopsForm(
    Player&            player,
    const std::string& ownerUuid,
    int                currentPage   = 0,
    bool               isRecycle     = false,
    OfficialFilter     officialFilter = OfficialFilter::All,
    PublicListSortMode sortMode       = PublicListSortMode::Sales,
    std::function<void(Player&)> onBack = {}
);

} // namespace CT
