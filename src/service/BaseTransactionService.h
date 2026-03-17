#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/Container.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace CT {

/**
 * @brief 交易服务基类
 * 提供箱子物品操作的通用方法
 */
class BaseTransactionService {
protected:
    // 获取箱子的容器视图（大箱子会返回合并后的容器）
    static Container* getChestContainer(BlockSource& region, BlockPos pos);

    // 从指定槽位安全移除物品
    static void removeItemsFromSlot(Container* container, int slot, int count);

    // 从箱子移除物品
    static int removeItemsFromChest(Container* container, const std::string& itemNbt, int count);

    // 向箱子添加物品
    static bool addItemsToChest(Container* container, const std::string& itemNbt, int count);

    // 计算箱子中匹配物品的数量
    static int countMatchingItems(Container* container, const std::string& itemNbt);

    // 批量计算箱子中多种物品的数量（单次遍历）
    static std::unordered_map<std::string, int>
    countAllMatchingItems(Container* container, const std::vector<std::string>& itemNbtList);
};

} // namespace CT
