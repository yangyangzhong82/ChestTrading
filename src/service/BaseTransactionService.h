#pragma once

#include "mc/world/actor/player/Player.h"
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
    // 获取箱子实体
    static ChestBlockActor* getChestActor(BlockSource& region, BlockPos pos);

    // 从箱子移除物品
    static int removeItemsFromChest(ChestBlockActor* chest, const std::string& itemNbt, int count);

    // 向箱子添加物品
    static bool addItemsToChest(ChestBlockActor* chest, const std::string& itemNbt, int count);

    // 计算箱子中匹配物品的数量
    static int countMatchingItems(ChestBlockActor* chest, const std::string& itemNbt);

    // 批量计算箱子中多种物品的数量（单次遍历）
    static std::unordered_map<std::string, int>
    countAllMatchingItems(ChestBlockActor* chest, const std::vector<std::string>& itemNbtList);
};

} // namespace CT
