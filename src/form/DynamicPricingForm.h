#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"

namespace CT {

// 显示动态价格设置表单（管理员用）
void showDynamicPricingForm(Player& player, BlockPos pos, int dimId, int itemId, bool isShop);

// 显示动态价格列表（查看当前商店所有动态价格配置）
void showDynamicPricingListForm(Player& player, BlockPos pos, int dimId, bool isShop);

} // namespace CT
