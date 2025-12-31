#include "test/TestHelper.h"

#include "Utils/NbtUtils.h"
#include "Utils/economy.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "service/ChestService.h"
#include "service/RecycleService.h"
#include "service/ShopService.h"
#include <chrono>
#include <sstream>
#include <string>

namespace CT::Test {

TestHelper& TestHelper::getInstance() {
    static TestHelper instance;
    return instance;
}

// === 辅助方法 ===

std::string TestHelper::formatTestResult(const std::string& testName, bool passed, const std::string& details) {
    std::ostringstream oss;
    oss << (passed ? "§a✓" : "§c✗") << " " << testName;
    if (!details.empty()) {
        oss << "\n  §7" << details;
    }
    return oss.str();
}

bool TestHelper::giveTestItems(Player& player) {
    // 给予测试物品：钻石剑 x5, 苹果 x64
    auto diamondSword = ItemStack("minecraft:diamond_sword", 5);
    auto apple        = ItemStack("minecraft:apple", 64);

    if (!player.add(diamondSword) || !player.add(apple)) {
        return false;
    }
    return true;
}

bool TestHelper::createChestAt(Player& player, BlockPos pos) {
    auto& region = player.getDimensionBlockSource();

    // 检查该位置是否已经有箱子
    auto* blockEntity = region.getBlockEntity(pos);
    if (!blockEntity || !dynamic_cast<ChestBlockActor*>(blockEntity)) {
        player.sendMessage("§c该位置没有箱子！请先手动放置一个箱子。");
        return false;
    }

    return true;
}

// === 测试箱子管理 ===

std::optional<BlockPos> TestHelper::createTestShopChest(Player& player) {
    auto  playerPos = player.getPosition();
    auto& region    = player.getDimensionBlockSource();
    int   dimId     = static_cast<int>(player.getDimensionId());

    // 在玩家前方 3 格查找箱子
    BlockPos chestPos(static_cast<int>(playerPos.x) + 3, static_cast<int>(playerPos.y), static_cast<int>(playerPos.z));

    // 检查该位置是否有箱子
    auto* blockEntity = region.getBlockEntity(chestPos);
    if (!blockEntity || !dynamic_cast<ChestBlockActor*>(blockEntity)) {
        player.sendMessage(
            "§c请在你前方 3 格处放置一个箱子！坐标: [" + std::to_string(chestPos.x) + ", " + std::to_string(chestPos.y)
            + ", " + std::to_string(chestPos.z) + "]"
        );
        return std::nullopt;
    }

    // 设置为商店
    auto& chestService = ChestService::getInstance();
    auto  result = chestService.createChest(player.getUuid().asString(), chestPos, dimId, ChestType::Shop, region);

    if (!result.success) {
        player.sendMessage("§c设置商店失败: " + result.message);
        return std::nullopt;
    }

    // 记录测试箱子
    mTestChests.emplace_back(dimId, chestPos, std::chrono::system_clock::now().time_since_epoch().count());

    player.sendMessage(
        "§a测试商店箱子已创建在 [" + std::to_string(chestPos.x) + ", " + std::to_string(chestPos.y) + ", "
        + std::to_string(chestPos.z) + "]"
    );
    return chestPos;
}

std::optional<BlockPos> TestHelper::createTestRecycleChest(Player& player) {
    auto  playerPos = player.getPosition();
    auto& region    = player.getDimensionBlockSource();
    int   dimId     = static_cast<int>(player.getDimensionId());

    BlockPos chestPos(static_cast<int>(playerPos.x) + 3, static_cast<int>(playerPos.y), static_cast<int>(playerPos.z));

    // 检查该位置是否有箱子
    auto* blockEntity = region.getBlockEntity(chestPos);
    if (!blockEntity || !dynamic_cast<ChestBlockActor*>(blockEntity)) {
        player.sendMessage(
            "§c请在你前方 3 格处放置一个箱子！坐标: [" + std::to_string(chestPos.x) + ", " + std::to_string(chestPos.y)
            + ", " + std::to_string(chestPos.z) + "]"
        );
        return std::nullopt;
    }

    auto& chestService = ChestService::getInstance();
    auto  result =
        chestService.createChest(player.getUuid().asString(), chestPos, dimId, ChestType::RecycleShop, region);

    if (!result.success) {
        player.sendMessage("§c设置回收商店失败: " + result.message);
        return std::nullopt;
    }

    mTestChests.emplace_back(dimId, chestPos, std::chrono::system_clock::now().time_since_epoch().count());

    player.sendMessage(
        "§a测试回收商店箱子已创建在 [" + std::to_string(chestPos.x) + ", " + std::to_string(chestPos.y) + ", "
        + std::to_string(chestPos.z) + "]"
    );
    return chestPos;
}

bool TestHelper::cleanupTestChest(Player& player, BlockPos pos) {
    auto& region = player.getDimensionBlockSource();
    int   dimId  = static_cast<int>(player.getDimensionId());

    // 从数据库删除
    auto& chestService = ChestService::getInstance();
    chestService.removeChest(pos, dimId, region);

    // 清空箱子
    auto* blockEntity = region.getBlockEntity(pos);
    if (auto* chest = dynamic_cast<ChestBlockActor*>(blockEntity)) {
        chest->clearInventory(0);
    }

    // 删除方块
    region.removeBlock(pos, BlockChangeContext{});

    // 从记录中移除
    mTestChests.erase(
        std::remove_if(
            mTestChests.begin(),
            mTestChests.end(),
            [&](const auto& entry) { return std::get<1>(entry) == pos && std::get<0>(entry) == dimId; }
        ),
        mTestChests.end()
    );

    return true;
}

// === 商店测试 ===

std::string TestHelper::testShopPurchase(Player& player, bool autoCleanup) {
    std::ostringstream report;
    report << "§e=== 商店购买测试 ===\n";

    // 1. 创建测试箱子
    auto chestPosOpt = createTestShopChest(player);
    if (!chestPosOpt.has_value()) {
        return report.str() + formatTestResult("创建测试箱子", false, "箱子创建失败");
    }
    BlockPos chestPos = chestPosOpt.value();
    int      dimId    = static_cast<int>(player.getDimensionId());
    auto&    region   = player.getDimensionBlockSource();

    report << formatTestResult(
        "创建测试箱子",
        true,
        "[" + std::to_string(chestPos.x) + ", " + std::to_string(chestPos.y) + ", " + std::to_string(chestPos.z) + "]"
    ) << "\n";

    // 2. 在箱子中放入测试物品
    auto* blockEntity = region.getBlockEntity(chestPos);
    auto* chest       = dynamic_cast<ChestBlockActor*>(blockEntity);
    if (!chest) {
        return report.str() + formatTestResult("获取箱子容器", false);
    }

    auto item = ItemStack("minecraft:diamond", 10);
    chest->setItem(0, item);

    report << formatTestResult("填充测试物品", true, "钻石 x10") << "\n";

    // 3. 设置价格
    auto itemNbt = NbtUtils::getItemNbt(item);
    if (!itemNbt) {
        if (autoCleanup) cleanupTestChest(player, chestPos);
        return report.str() + formatTestResult("序列化物品NBT", false);
    }

    auto& shopService = ShopService::getInstance();
    auto  priceResult = shopService.setItemPrice(chestPos, dimId, itemNbt->toSnbt(), 100.0, 10, region);

    if (!priceResult.success) {
        if (autoCleanup) cleanupTestChest(player, chestPos);
        return report.str() + formatTestResult("设置物品价格", false, priceResult.message);
    }

    report << formatTestResult("设置物品价格", true, "100金币/个") << "\n";

    // 4. 记录玩家初始金币
    auto initialMoney = Economy::getMoney(player);

    // 5. 购买物品（购买 3 个）
    auto purchaseResult =
        shopService.purchaseItem(player, chestPos, dimId, priceResult.itemId, 3, region, itemNbt->toSnbt());

    bool purchaseSuccess =
        purchaseResult.success && purchaseResult.itemsReceived == 3 && purchaseResult.totalCost == 300.0;

    report << formatTestResult("购买物品", purchaseSuccess, purchaseResult.message) << "\n";

    if (purchaseSuccess) {
        // 验证金币扣除
        auto finalMoney = Economy::getMoney(player);
        bool moneyCheck = (initialMoney - finalMoney) == 300.0;
        report << formatTestResult("验证金币扣除", moneyCheck, "扣除: 300金币") << "\n";

        // 验证库存变化
        int  remainingCount = shopService.countItemsInChest(region, chestPos, dimId, itemNbt->toSnbt());
        bool inventoryCheck = remainingCount == 7; // 10 - 3 = 7
        report << formatTestResult("验证库存变化", inventoryCheck, "剩余: " + std::to_string(remainingCount)) << "\n";
    }

    // 6. 清理
    if (autoCleanup) {
        cleanupTestChest(player, chestPos);
        report << "§7测试箱子已清理\n";
    }

    return report.str();
}

std::string TestHelper::testShopInventorySync(Player& player) {
    std::ostringstream report;
    report << "§e=== 商店库存同步测试 ===\n";

    auto chestPosOpt = createTestShopChest(player);
    if (!chestPosOpt.has_value()) {
        return report.str() + formatTestResult("创建测试箱子", false);
    }

    BlockPos chestPos = chestPosOpt.value();
    int      dimId    = static_cast<int>(player.getDimensionId());
    auto&    region   = player.getDimensionBlockSource();

    // 测试：设置价格后手动修改箱子库存
    auto* chest = static_cast<ChestBlockActor*>(region.getBlockEntity(chestPos));
    auto  item  = ItemStack("minecraft:iron_ingot", 20);
    chest->setItem(0, item);

    auto  itemNbt     = NbtUtils::getItemNbt(item);
    auto& shopService = ShopService::getInstance();
    auto  priceResult = shopService.setItemPrice(chestPos, dimId, itemNbt->toSnbt(), 50.0, 20, region);

    report << formatTestResult("设置初始价格", priceResult.success, "铁锭 x20, 50金币/个") << "\n";

    // 手动从箱子移除 5 个
    auto updatedItem = ItemStack("minecraft:iron_ingot", 15);
    chest->setItem(0, updatedItem);

    // 查询库存（应该读取箱子实际数量）
    int  actualCount = shopService.countItemsInChest(region, chestPos, dimId, itemNbt->toSnbt());
    bool syncCheck   = actualCount == 15;

    report << formatTestResult("库存同步检查", syncCheck, "实际: " + std::to_string(actualCount) + ", 预期: 15")
           << "\n";

    cleanupTestChest(player, chestPos);
    return report.str();
}

std::string TestHelper::testShopPriceUpdate(Player& player) {
    std::ostringstream report;
    report << "§e=== 商店价格更新测试 ===\n";

    auto chestPosOpt = createTestShopChest(player);
    if (!chestPosOpt.has_value()) {
        return report.str() + formatTestResult("创建测试箱子", false);
    }

    BlockPos chestPos = chestPosOpt.value();
    int      dimId    = static_cast<int>(player.getDimensionId());
    auto&    region   = player.getDimensionBlockSource();

    auto* chest = static_cast<ChestBlockActor*>(region.getBlockEntity(chestPos));
    auto  item  = ItemStack("minecraft:gold_ingot", 10);
    chest->setItem(0, item);

    auto  itemNbt     = NbtUtils::getItemNbt(item);
    auto& shopService = ShopService::getInstance();

    // 设置初始价格 100
    auto result1 = shopService.setItemPrice(chestPos, dimId, itemNbt->toSnbt(), 100.0, 10, region);
    report << formatTestResult("设置初始价格", result1.success, "100金币/个") << "\n";

    // 更新价格为 200（重新设置）
    auto result2 = shopService.setItemPrice(chestPos, dimId, itemNbt->toSnbt(), 200.0, 10, region);
    report << formatTestResult("更新价格", result2.success, "200金币/个") << "\n";

    // 验证：购买时应使用新价格 200
    auto initialMoney = Economy::getMoney(player);
    auto purchaseResult =
        shopService.purchaseItem(player, chestPos, dimId, result2.itemId, 1, region, itemNbt->toSnbt());

    bool priceCheck = purchaseResult.success && purchaseResult.totalCost == 200.0;
    report << formatTestResult("验证新价格生效", priceCheck, "扣除: " + std::to_string(purchaseResult.totalCost))
           << "\n";

    cleanupTestChest(player, chestPos);
    return report.str();
}

// === 回收测试 ===

std::string TestHelper::testRecycle(Player& player, bool autoCleanup) {
    std::ostringstream report;
    report << "§e=== 回收流程测试 ===\n";

    // 1. 创建回收商店箱子
    auto chestPosOpt = createTestRecycleChest(player);
    if (!chestPosOpt.has_value()) {
        return report.str() + formatTestResult("创建回收箱子", false);
    }

    BlockPos chestPos = chestPosOpt.value();
    int      dimId    = static_cast<int>(player.getDimensionId());
    auto&    region   = player.getDimensionBlockSource();

    report << formatTestResult(
        "创建回收箱子",
        true,
        "[" + std::to_string(chestPos.x) + ", " + std::to_string(chestPos.y) + ", " + std::to_string(chestPos.z) + "]"
    ) << "\n";

    // 2. 设置回收委托（回收钻石剑）
    auto item    = ItemStack("minecraft:diamond_sword", 1);
    auto itemNbt = NbtUtils::getItemNbt(item);
    if (!itemNbt) {
        if (autoCleanup) cleanupTestChest(player, chestPos);
        return report.str() + formatTestResult("序列化物品NBT", false);
    }

    auto& recycleService   = RecycleService::getInstance();
    auto  commissionResult = recycleService.setCommission(chestPos, dimId, itemNbt->toSnbt(), 150.0, 0, "", 10, -1);

    if (!commissionResult.success) {
        if (autoCleanup) cleanupTestChest(player, chestPos);
        return report.str() + formatTestResult("设置回收委托", false, commissionResult.message);
    }

    report << formatTestResult("设置回收委托", true, "钻石剑, 150金币/个") << "\n";

    // 3. 给玩家钻石剑
    auto testSword = ItemStack("minecraft:diamond_sword", 5);
    player.add(testSword);
    report << formatTestResult("给予测试物品", true, "钻石剑 x5") << "\n";

    // 4. 记录初始金币
    auto initialMoney = Economy::getMoney(player);

    // 5. 执行回收（回收 2 个）
    auto recycleResult =
        recycleService
            .executeFullRecycle(player, chestPos, dimId, commissionResult.itemId, 2, 150.0, itemNbt->toSnbt(), region);

    bool recycleSuccess =
        recycleResult.success && recycleResult.itemsRecycled == 2 && recycleResult.totalEarned == 300.0;

    report << formatTestResult("回收物品", recycleSuccess, recycleResult.message) << "\n";

    if (recycleSuccess) {
        // 验证金币增加
        auto finalMoney = Economy::getMoney(player);
        bool moneyCheck = (finalMoney - initialMoney) == 300.0;
        report << formatTestResult("验证金币增加", moneyCheck, "增加: 300金币") << "\n";

        // 验证箱子中有物品
        auto* chest     = static_cast<ChestBlockActor*>(region.getBlockEntity(chestPos));
        int   itemCount = 0;
        for (int i = 0; i < chest->getContainerSize(); ++i) {
            auto& slotItem = chest->getItem(i);
            if (!slotItem.isNull() && slotItem.getTypeName() == "minecraft:diamond_sword") {
                itemCount += slotItem.mCount;
            }
        }
        bool chestCheck = itemCount == 2;
        report << formatTestResult("验证箱子收到物品", chestCheck, "箱子中: " + std::to_string(itemCount)) << "\n";
    }

    // 6. 清理
    if (autoCleanup) {
        cleanupTestChest(player, chestPos);
        report << "§7测试箱子已清理\n";
    }

    return report.str();
}

std::string TestHelper::testRecycleFilters(Player& player) {
    std::ostringstream report;
    report << "§e=== 回收过滤器测试 ===\n";

    // TODO: 实现耐久度和附魔过滤测试
    report << formatTestResult("耐久度过滤", false, "待实现") << "\n";
    report << formatTestResult("附魔过滤", false, "待实现") << "\n";

    return report.str();
}

std::string TestHelper::testRecycleRollback(Player& player) {
    std::ostringstream report;
    report << "§e=== 回收回滚测试 ===\n";

    // TODO: 实现回滚机制测试（模拟失败场景）
    report << formatTestResult("回滚机制", false, "待实现") << "\n";

    return report.str();
}

// === 综合测试 ===

std::string TestHelper::runAllTests(Player& player) {
    std::ostringstream fullReport;
    fullReport << "§6========== ChestTrading 自动化测试 ==========\n\n";

    fullReport << testShopPurchase(player, true) << "\n";
    fullReport << testShopInventorySync(player) << "\n";
    fullReport << testShopPriceUpdate(player) << "\n";
    fullReport << testRecycle(player, true) << "\n";
    fullReport << testRecycleFilters(player) << "\n";
    fullReport << testRecycleRollback(player) << "\n";

    fullReport << "§6========== 测试完成 ==========\n";
    return fullReport.str();
}

} // namespace CT::Test
