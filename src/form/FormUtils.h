#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/nbt/CompoundTag.h"
#include "Utils/ItemTextureManager.h"
#include "Utils/NbtUtils.h"
#include "logger.h"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <tuple>


namespace CT::FormUtils {

/**
 * @brief 生成物品的显示文本，包含名称、类型、耐久度、特殊值和附魔信息。
 * @param item 物品的ItemStack。
 * @param count 物品数量，可选，如果提供则显示在名称后。
 * @param showTypeName 是否显示物品类型名称。
 * @return 包含物品所有信息的格式化字符串。
 */
std::string getItemDisplayString(const ItemStack& item, int count = 0, bool showTypeName = true);

/**
 * @brief 获取物品的纹理路径。
 * @param item 物品的ItemStack。
 * @return 物品的纹理路径字符串。
 */
std::string getItemTexturePath(const ItemStack& item);

/**
 * @brief 从SNBT字符串解析NBT并创建ItemStack。
 * @param itemNbtStr 物品的SNBT字符串。
 * @return 创建的ItemStack指针，如果失败则为nullptr。
 */
std::unique_ptr<ItemStack> createItemStackFromNbtString(const std::string& itemNbtStr);

/**
 * @brief 迭代箱子内容，根据提供的NBT字符串计算匹配物品的总数量。
 * @param region 区块源。
 * @param pos 箱子的位置。
 * @param dimId 箱子所在的维度ID。
 * @param targetItemNbtStr 目标物品的SNBT字符串（已清理）。
 * @return 箱子中匹配物品的总数量。
 */
int countItemsInChest(BlockSource& region, BlockPos pos, int dimId, const std::string& targetItemNbtStr);

} // namespace CT::FormUtils
