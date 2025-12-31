#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace CT::Test {

/**
 * @brief 测试助手类，提供游戏内自动化测试功能
 */
class TestHelper {
public:
    static TestHelper& getInstance();

    TestHelper(const TestHelper&)            = delete;
    TestHelper& operator=(const TestHelper&) = delete;

    // === 测试箱子管理 ===

    /**
     * @brief 在玩家面前创建测试商店箱子
     * @return 箱子坐标，失败返回 std::nullopt
     */
    std::optional<BlockPos> createTestShopChest(Player& player);

    /**
     * @brief 在玩家面前创建测试回收商店箱子
     * @return 箱子坐标，失败返回 std::nullopt
     */
    std::optional<BlockPos> createTestRecycleChest(Player& player);

    /**
     * @brief 清理测试箱子
     */
    bool cleanupTestChest(Player& player, BlockPos pos);

    // === 商店测试 ===

    /**
     * @brief 自动测试商店购买流程
     * @param player 测试玩家
     * @param autoCleanup 测试完成后是否自动清理
     * @return 测试报告
     */
    std::string testShopPurchase(Player& player, bool autoCleanup = true);

    /**
     * @brief 测试商店库存同步
     */
    std::string testShopInventorySync(Player& player);

    /**
     * @brief 测试商店价格更新
     */
    std::string testShopPriceUpdate(Player& player);

    // === 回收测试 ===

    /**
     * @brief 自动测试回收流程
     * @param player 测试玩家
     * @param autoCleanup 测试完成后是否自动清理
     * @return 测试报告
     */
    std::string testRecycle(Player& player, bool autoCleanup = true);

    /**
     * @brief 测试回收过滤器（耐久度、附魔）
     */
    std::string testRecycleFilters(Player& player);

    /**
     * @brief 测试回收回滚机制
     */
    std::string testRecycleRollback(Player& player);

    // === 综合测试 ===

    /**
     * @brief 运行所有测试
     * @return 测试摘要
     */
    std::string runAllTests(Player& player);

private:
    TestHelper() = default;

    // 测试箱子标记（用于清理）
    std::vector<std::tuple<int, BlockPos, int>> mTestChests; // dimId, pos, timestamp

    // 工具方法
    bool giveTestItems(Player& player);
    bool createChestAt(Player& player, BlockPos pos);
    std::string formatTestResult(const std::string& testName, bool passed, const std::string& details = "");
};

} // namespace CT::Test
