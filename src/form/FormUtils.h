#pragma once

#include "Utils/ItemTextureManager.h"
#include "Utils/NbtUtils.h"
#include "logger.h"
#include "mc/nbt/CompoundTag.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>


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

/**
 * @brief 显示设置商店名称的通用表单
 * @param player 玩家
 * @param pos 箱子位置
 * @param dimId 维度ID
 * @param onComplete 完成后的回调函数
 */
void showSetNameForm(
    Player&                                     player,
    BlockPos                                    pos,
    int                                         dimId,
    const std::string&                          title,
    std::function<void(Player&, BlockPos, int)> onComplete
);

/**
 * @brief 执行传送到商店的通用逻辑（检查冷却、扣费、传送）
 * @param player 玩家
 * @param pos 目标位置
 * @param dimId 目标维度
 * @return 是否传送成功
 */
bool teleportToShop(Player& player, BlockPos pos, int dimId);

/**
 * @brief 将维度ID转换为本地化的维度名称
 * @param dimId 维度ID (0=主世界, 1=下界, 2=末地)
 * @return 本地化的维度名称字符串
 */
std::string dimIdToString(int dimId);

/**
 * @brief 批量获取玩家名称并缓存，避免 N+1 查询
 * @param uuids 玩家 UUID 列表
 * @return UUID 到玩家名称的映射
 */
std::map<std::string, std::string> getPlayerNameCache(const std::vector<std::string>& uuids);

} // namespace CT::FormUtils
