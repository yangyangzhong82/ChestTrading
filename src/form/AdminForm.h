#pragma once

#include "Types.h"
#include "mc/world/actor/player/Player.h"
#include <functional>
#include <optional>
#include <vector>

namespace CT {

// 坐标范围筛选结构
struct CoordinateRange {
    std::optional<int> minX, maxX, minY, maxY, minZ, maxZ;
};

// 主筛选表单
void showAdminMainForm(Player& player);
void showAdminChestUi(
    Player&                       player,
    int                           currentPage = 1,
    const std::vector<int>&       dimIdFilter = {},
    const std::vector<ChestType>& chestTypeFilter = {},
    const CoordinateRange&        coordRange = {},
    std::function<void(Player&)>  onBack = {}
);

// 箱子列表表单，增加了筛选参数
void showAdminForm(
    Player&                       player,
    int                           currentPage,
    const std::vector<int>&       dimIdFilter,
    const std::vector<ChestType>& chestTypeFilter,
    const CoordinateRange&        coordRange
);

} // namespace CT
