#pragma once

#include "interaction/chestprotect.h" // 包含 ChestType 定义
#include "mc/world/actor/player/Player.h"
#include <optional>
#include <vector>

namespace CT {

// 主筛选表单
void showAdminMainForm(Player& player);

// 坐标范围筛选结构
struct CoordinateRange {
    std::optional<int> minX, maxX, minY, maxY, minZ, maxZ;
};

// 箱子列表表单，增加了筛选参数
void showAdminForm(
    Player&                       player,
    int                           currentPage,
    const std::vector<int>&       dimIdFilter,
    const std::vector<ChestType>& chestTypeFilter,
    const CoordinateRange&        coordRange
);

} // namespace CT
