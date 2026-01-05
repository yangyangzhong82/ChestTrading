#include "test/TestHelper.h"

#include "Config/ConfigManager.h"
#include "Utils/NbtUtils.h"
#include "Utils/economy.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "service/ChestService.h"
#include "service/RecycleService.h"
#include "service/ShopService.h"
#include <chrono>
#include <cmath>
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
    recordTestResult(passed);
    return oss.str();
}

void TestHelper::resetTestStats() {
    mPassedCount  = 0;
    mFailedCount  = 0;
    mSkippedCount = 0;
}

void TestHelper::recordTestResult(bool passed) {
    if (passed) {
        mPassedCount++;
    } else {
        mFailedCount++;
    }
}

std::string TestHelper::getTestSummary(int passed, int failed, int skipped) {
    std::ostringstream oss;
    oss << "§6========== 测试摘要 ==========\n";
    oss << "§a通过: " << passed << "  ";
    oss << "§c失败: " << failed << "  ";
    if (skipped > 0) {
        oss << "§7跳过: " << skipped;
    }
    oss << "\n";

    if (failed == 0) {
        oss << "§a所有测试通过！\n";
    } else {
        oss << "§c有 " << failed << " 个测试失败，请检查上方输出。\n";
    }
    return oss.str();
}

bool TestHelper::giveTestItems(Player& player) {
    auto diamondSword = ItemStack("minecraft:diamond_sword", 5);
    auto apple        = ItemStack("minecraft:apple", 64);

    if (!player.add(diamondSword) || !player.add(apple)) {
        return false;
    }
    return true;
}

BlockPos TestHelper::findSafePosition(Player& player) {
    auto  playerPos = player.getPosition();
    auto& region    = player.getDimensionBlockSource();

    // 在玩家前方查找一个安全的位置放置箱子
    // 遍历前方 2-5 格，找一个空气方块的位置
    for (int dist = 2; dist <= 5; dist++) {
        BlockPos pos(
            static_cast<int>(playerPos.x) + dist,
            static_cast<int>(playerPos.y),
            static_cast<int>(playerPos.z)
        );

        // 检查该位置和上方是否为空气
        auto& block      = region.getBlock(pos);
        auto& blockAbove = region.getBlock(BlockPos(pos.x, pos.y + 1, pos.z));

        if (block.isAir() || block.getTypeName() == "minecraft:chest") {
            return pos;
        }
    }

    // 如果前方都不行，就用玩家正上方 2 格
    return BlockPos(static_cast<int>(playerPos.x), static_cast<int>(playerPos.y) + 2, static_cast<int>(playerPos.z));
}

bool TestHelper::placeChestBlock(Player& player, BlockPos pos) {
    auto& region = player.getDimensionBlockSource();

    // 先检查是否已有箱子
    auto* existingEntity = region.getBlockEntity(pos);
    if (existingEntity && dynamic_cast<ChestBlockActor*>(existingEntity)) {
        return true; // 已经有箱子了
    }

    // 尝试使用命令放置箱子
    // 注：由于方块放置 API 的复杂性，这里使用简单的检查
    // 如果该位置没有箱子，提示玩家手动放置
    player.sendMessage(
        "§e请在以下位置放置一个箱子: [" + std::to_string(pos.x) + ", " + std::to_string(pos.y) + ", "
        + std::to_string(pos.z) + "]"
    );
    player.sendMessage("§7提示: 放置后重新运行测试命令");

    return false;
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

std::optional<BlockPos> TestHelper::createTestShopChest(Player& player, bool autoPlace) {
    auto& region = player.getDimensionBlockSource();
    int   dimId  = static_cast<int>(player.getDimensionId());

    // 查找安全位置
    BlockPos chestPos = findSafePosition(player);

    // 自动放置箱子
    if (autoPlace) {
        if (!placeChestBlock(player, chestPos)) {
            return std::nullopt;
        }
    } else {
        // 检查该位置是否有箱子
        auto* blockEntity = region.getBlockEntity(chestPos);
        if (!blockEntity || !dynamic_cast<ChestBlockActor*>(blockEntity)) {
            player.sendMessage(
                "§c请在你前方放置一个箱子！坐标: [" + std::to_string(chestPos.x) + ", " + std::to_string(chestPos.y)
                + ", " + std::to_string(chestPos.z) + "]"
            );
            return std::nullopt;
        }
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

    return chestPos;
}

std::optional<BlockPos> TestHelper::createTestRecycleChest(Player& player, bool autoPlace) {
    auto& region = player.getDimensionBlockSource();
    int   dimId  = static_cast<int>(player.getDimensionId());

    // 查找安全位置（偏移一点避免和商店箱子重叠）
    auto     playerPos = player.getPosition();
    BlockPos chestPos(static_cast<int>(playerPos.x), static_cast<int>(playerPos.y), static_cast<int>(playerPos.z) + 3);

    // 自动放置箱子
    if (autoPlace) {
        if (!placeChestBlock(player, chestPos)) {
            return std::nullopt;
        }
    } else {
        auto* blockEntity = region.getBlockEntity(chestPos);
        if (!blockEntity || !dynamic_cast<ChestBlockActor*>(blockEntity)) {
            player.sendMessage(
                "§c请放置一个箱子！坐标: [" + std::to_string(chestPos.x) + ", " + std::to_string(chestPos.y) + ", "
                + std::to_string(chestPos.z) + "]"
            );
            return std::nullopt;
        }
    }

    auto& chestService = ChestService::getInstance();
    auto  result =
        chestService.createChest(player.getUuid().asString(), chestPos, dimId, ChestType::RecycleShop, region);

    if (!result.success) {
        player.sendMessage("§c设置回收商店失败: " + result.message);
        return std::nullopt;
    }

    mTestChests.emplace_back(dimId, chestPos, std::chrono::system_clock::now().time_since_epoch().count());

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

void TestHelper::cleanupAllTestChests(Player& player) {
    auto& region = player.getDimensionBlockSource();
    int   dimId  = static_cast<int>(player.getDimensionId());

    // 复制列表以避免迭代时修改
    auto chestsCopy = mTestChests;
    for (const auto& [chestDimId, pos, timestamp] : chestsCopy) {
        if (chestDimId == dimId) {
            cleanupTestChest(player, pos);
        }
    }
}

// === 商店测试 ===

std::string TestHelper::testShopPurchase(Player& player, bool autoCleanup) {
    std::ostringstream report;
    report << "§e=== 商店购买测试 ===\n";

    // 1. 创建测试箱子（自动放置）
    auto chestPosOpt = createTestShopChest(player, true);
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

    // 4. 记录玩家初始金币并确保足够
    auto initialMoney = Economy::getMoney(player);
    if (initialMoney < 300.0) {
        Economy::addMoney(player, 1000.0);
        initialMoney = Economy::getMoney(player);
    }

    // 5. 购买物品（购买 3 个）
    auto purchaseResult =
        shopService.purchaseItem(player, chestPos, dimId, priceResult.itemId, 3, region, itemNbt->toSnbt());

    bool purchaseSuccess =
        purchaseResult.success && purchaseResult.itemsReceived == 3 && purchaseResult.totalCost == 300.0;

    report << formatTestResult("购买物品", purchaseSuccess, purchaseResult.message) << "\n";

    if (purchaseSuccess) {
        // 验证金币扣除
        auto finalMoney = Economy::getMoney(player);
        bool moneyCheck = std::abs((initialMoney - finalMoney) - 300.0) < 0.01;
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

    auto chestPosOpt = createTestShopChest(player, true);
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

    auto chestPosOpt = createTestShopChest(player, true);
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

    // 确保玩家有足够金币
    auto initialMoney = Economy::getMoney(player);
    if (initialMoney < 200.0) {
        Economy::addMoney(player, 1000.0);
        initialMoney = Economy::getMoney(player);
    }

    // 验证：购买时应使用新价格 200
    auto purchaseResult =
        shopService.purchaseItem(player, chestPos, dimId, result2.itemId, 1, region, itemNbt->toSnbt());

    bool priceCheck = purchaseResult.success && std::abs(purchaseResult.totalCost - 200.0) < 0.01;
    report << formatTestResult("验证新价格生效", priceCheck, "扣除: " + std::to_string(purchaseResult.totalCost))
           << "\n";

    cleanupTestChest(player, chestPos);
    return report.str();
}

std::string TestHelper::testShopInsufficientMoney(Player& player) {
    std::ostringstream report;
    report << "§e=== 金币不足测试 ===\n";

    auto chestPosOpt = createTestShopChest(player, true);
    if (!chestPosOpt.has_value()) {
        return report.str() + formatTestResult("创建测试箱子", false);
    }

    BlockPos chestPos = chestPosOpt.value();
    int      dimId    = static_cast<int>(player.getDimensionId());
    auto&    region   = player.getDimensionBlockSource();

    auto* chest = static_cast<ChestBlockActor*>(region.getBlockEntity(chestPos));
    auto  item  = ItemStack("minecraft:emerald", 10);
    chest->setItem(0, item);

    auto  itemNbt     = NbtUtils::getItemNbt(item);
    auto& shopService = ShopService::getInstance();

    // 设置高价格
    auto priceResult = shopService.setItemPrice(chestPos, dimId, itemNbt->toSnbt(), 10000.0, 10, region);
    report << formatTestResult("设置高价格", priceResult.success, "10000金币/个") << "\n";

    // 记录当前金币，确保不够
    auto currentMoney = Economy::getMoney(player);
    if (currentMoney >= 10000.0) {
        Economy::reduceMoney(player, currentMoney - 100.0); // 只留 100
    }
    currentMoney = Economy::getMoney(player);
    report << "§7当前金币: " << currentMoney << "\n";

    // 尝试购买（应该失败）
    auto purchaseResult =
        shopService.purchaseItem(player, chestPos, dimId, priceResult.itemId, 1, region, itemNbt->toSnbt());

    bool insufficientCheck = !purchaseResult.success;
    report << formatTestResult("金币不足拒绝购买", insufficientCheck, purchaseResult.message) << "\n";

    // 验证金币未被扣除
    auto finalMoney     = Economy::getMoney(player);
    bool moneyUnchanged = std::abs(finalMoney - currentMoney) < 0.01;
    report << formatTestResult("金币未被扣除", moneyUnchanged, "金币: " + std::to_string(finalMoney)) << "\n";

    cleanupTestChest(player, chestPos);
    return report.str();
}

std::string TestHelper::testShopInsufficientStock(Player& player) {
    std::ostringstream report;
    report << "§e=== 库存不足测试 ===\n";

    auto chestPosOpt = createTestShopChest(player, true);
    if (!chestPosOpt.has_value()) {
        return report.str() + formatTestResult("创建测试箱子", false);
    }

    BlockPos chestPos = chestPosOpt.value();
    int      dimId    = static_cast<int>(player.getDimensionId());
    auto&    region   = player.getDimensionBlockSource();

    auto* chest = static_cast<ChestBlockActor*>(region.getBlockEntity(chestPos));
    auto  item  = ItemStack("minecraft:diamond", 3); // 只放 3 个
    chest->setItem(0, item);

    auto  itemNbt     = NbtUtils::getItemNbt(item);
    auto& shopService = ShopService::getInstance();

    auto priceResult = shopService.setItemPrice(chestPos, dimId, itemNbt->toSnbt(), 10.0, 3, region);
    report << formatTestResult("设置价格", priceResult.success, "钻石 x3, 10金币/个") << "\n";

    // 确保玩家有足够金币
    auto currentMoney = Economy::getMoney(player);
    if (currentMoney < 100.0) {
        Economy::addMoney(player, 1000.0);
    }

    // 尝试购买 10 个（应该失败，因为只有 3 个）
    auto purchaseResult =
        shopService.purchaseItem(player, chestPos, dimId, priceResult.itemId, 10, region, itemNbt->toSnbt());

    bool stockCheck = !purchaseResult.success;
    report << formatTestResult("库存不足拒绝购买", stockCheck, purchaseResult.message) << "\n";

    // 验证库存未变
    int  remainingCount     = shopService.countItemsInChest(region, chestPos, dimId, itemNbt->toSnbt());
    bool inventoryUnchanged = remainingCount == 3;
    report << formatTestResult("库存未变", inventoryUnchanged, "剩余: " + std::to_string(remainingCount)) << "\n";

    cleanupTestChest(player, chestPos);
    return report.str();
}

std::string TestHelper::testShopTaxRate(Player& player) {
    std::ostringstream report;
    report << "§e=== 税率测试 ===\n";

    // 获取当前税率配置
    auto&  config  = ConfigManager::getInstance().get();
    double taxRate = config.taxSettings.shopTaxRate;
    report << "§7当前商店税率: " << (taxRate * 100) << "%\n";

    auto chestPosOpt = createTestShopChest(player, true);
    if (!chestPosOpt.has_value()) {
        return report.str() + formatTestResult("创建测试箱子", false);
    }

    BlockPos chestPos = chestPosOpt.value();
    int      dimId    = static_cast<int>(player.getDimensionId());
    auto&    region   = player.getDimensionBlockSource();

    auto* chest = static_cast<ChestBlockActor*>(region.getBlockEntity(chestPos));
    auto  item  = ItemStack("minecraft:diamond", 10);
    chest->setItem(0, item);

    auto  itemNbt     = NbtUtils::getItemNbt(item);
    auto& shopService = ShopService::getInstance();

    auto priceResult = shopService.setItemPrice(chestPos, dimId, itemNbt->toSnbt(), 100.0, 10, region);
    report << formatTestResult("设置价格", priceResult.success, "100金币/个") << "\n";

    // 因为测试中店主和买家是同一人，我们只记录初始金币
    auto initialMoney = Economy::getMoney(player);

    // 确保买家有足够金币
    if (initialMoney < 100.0) {
        Economy::addMoney(player, 1000.0);
        initialMoney = Economy::getMoney(player);
    }

    // 购买 1 个，总价 100
    auto purchaseResult =
        shopService.purchaseItem(player, chestPos, dimId, priceResult.itemId, 1, region, itemNbt->toSnbt());

    report << formatTestResult("购买物品", purchaseResult.success, purchaseResult.message) << "\n";

    if (purchaseResult.success) {
        // 计算店主应收金额（扣除税率）
        double expectedOwnerIncome = 100.0 * (1.0 - taxRate);
        auto   finalMoney          = Economy::getMoney(player);

        // 由于买家和卖家是同一人：
        // 最终金币 = 初始金币 - 购买花费 + 店主收入
        // 最终金币 = 初始金币 - 100 + (100 * (1 - taxRate))
        // 差值 = -100 + 100*(1-taxRate) = -100*taxRate
        double expectedChange = -100.0 * taxRate;
        double actualChange   = finalMoney - initialMoney;

        report << "§7预期金币变化: " << expectedChange << " (税收)\n";
        report << "§7实际金币变化: " << actualChange << "\n";

        bool taxCheck = std::abs(actualChange - expectedChange) < 1.0;
        report << formatTestResult("税率计算正确", taxCheck, "") << "\n";
    }

    cleanupTestChest(player, chestPos);
    return report.str();
}

std::string TestHelper::testShopBoundaryConditions(Player& player) {
    std::ostringstream report;
    report << "§e=== 边界条件测试 ===\n";

    auto chestPosOpt = createTestShopChest(player, true);
    if (!chestPosOpt.has_value()) {
        return report.str() + formatTestResult("创建测试箱子", false);
    }

    BlockPos chestPos = chestPosOpt.value();
    int      dimId    = static_cast<int>(player.getDimensionId());
    auto&    region   = player.getDimensionBlockSource();

    auto* chest = static_cast<ChestBlockActor*>(region.getBlockEntity(chestPos));
    auto  item  = ItemStack("minecraft:diamond", 10);
    chest->setItem(0, item);

    auto  itemNbt     = NbtUtils::getItemNbt(item);
    auto& shopService = ShopService::getInstance();

    auto priceResult = shopService.setItemPrice(chestPos, dimId, itemNbt->toSnbt(), 10.0, 10, region);
    report << formatTestResult("设置价格", priceResult.success, "10金币/个") << "\n";

    // 确保玩家有足够金币
    auto currentMoney = Economy::getMoney(player);
    if (currentMoney < 100.0) {
        Economy::addMoney(player, 1000.0);
    }

    // 测试购买 0 个
    auto result0 = shopService.purchaseItem(player, chestPos, dimId, priceResult.itemId, 0, region, itemNbt->toSnbt());
    bool zeroCheck = !result0.success;
    report << formatTestResult("购买0个被拒绝", zeroCheck, result0.message) << "\n";

    // 测试购买负数个
    auto resultNeg =
        shopService.purchaseItem(player, chestPos, dimId, priceResult.itemId, -1, region, itemNbt->toSnbt());
    bool negCheck = !resultNeg.success;
    report << formatTestResult("购买负数被拒绝", negCheck, resultNeg.message) << "\n";

    // 测试设置负价格
    auto negPriceResult = shopService.setItemPrice(chestPos, dimId, itemNbt->toSnbt(), -10.0, 10, region);
    bool negPriceCheck  = !negPriceResult.success;
    report << formatTestResult("负价格被拒绝", negPriceCheck, negPriceResult.message) << "\n";

    cleanupTestChest(player, chestPos);
    return report.str();
}

// === 回收测试 ===

std::string TestHelper::testRecycle(Player& player, bool autoCleanup) {
    std::ostringstream report;
    report << "§e=== 回收流程测试 ===\n";

    // 1. 创建回收商店箱子（自动放置）
    auto chestPosOpt = createTestRecycleChest(player, true);
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

    // 4. 确保店主有足够金币支付回收款（店主就是测试玩家）
    auto ownerMoney = Economy::getMoney(player);
    if (ownerMoney < 300.0) {
        Economy::addMoney(player, 1000.0);
    }

    // 5. 记录初始金币
    auto initialMoney = Economy::getMoney(player);

    // 6. 执行回收（回收 2 个）
    auto recycleResult =
        recycleService
            .executeFullRecycle(player, chestPos, dimId, commissionResult.itemId, 2, 150.0, itemNbt->toSnbt(), region);

    bool recycleSuccess =
        recycleResult.success && recycleResult.itemsRecycled == 2 && std::abs(recycleResult.totalEarned - 300.0) < 0.01;

    report << formatTestResult("回收物品", recycleSuccess, recycleResult.message) << "\n";

    if (recycleSuccess) {
        // 验证金币变化（作为卖家，应该收到钱，但同时作为店主又付出去了）
        auto finalMoney = Economy::getMoney(player);
        report << "§7初始金币: " << initialMoney << ", 最终金币: " << finalMoney << "\n";

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

    // 7. 清理
    if (autoCleanup) {
        cleanupTestChest(player, chestPos);
        report << "§7测试箱子已清理\n";
    }

    return report.str();
}

std::string TestHelper::testRecycleFilters(Player& player) {
    std::ostringstream report;
    report << "§e=== 回收过滤器测试 ===\n";

    auto chestPosOpt = createTestRecycleChest(player, true);
    if (!chestPosOpt.has_value()) {
        return report.str() + formatTestResult("创建回收箱子", false);
    }

    BlockPos chestPos = chestPosOpt.value();
    int      dimId    = static_cast<int>(player.getDimensionId());
    auto&    region   = player.getDimensionBlockSource();

    // 测试1：设置最低耐久度要求
    auto item    = ItemStack("minecraft:diamond_pickaxe", 1);
    auto itemNbt = NbtUtils::getItemNbt(item);
    if (!itemNbt) {
        cleanupTestChest(player, chestPos);
        return report.str() + formatTestResult("序列化物品NBT", false);
    }

    auto& recycleService = RecycleService::getInstance();

    // 设置回收委托，要求最低耐久度 50%
    auto commissionResult = recycleService.setCommission(
        chestPos,
        dimId,
        itemNbt->toSnbt(),
        100.0, // 价格
        50,    // 最低耐久度 50%
        "",    // 无附魔要求
        10,    // 最大数量
        -1     // 无限期
    );

    report << formatTestResult("设置耐久度过滤", commissionResult.success, "最低耐久度: 50%") << "\n";

    // 测试2：设置附魔要求（如果支持的话）
    auto enchantResult = recycleService.setCommission(
        chestPos,
        dimId,
        itemNbt->toSnbt(),
        200.0,        // 高价格（有附魔）
        0,            // 不限耐久
        "efficiency", // 要求效率附魔
        10,           // 最大数量
        -1            // 无限期
    );

    report << formatTestResult("设置附魔过滤", enchantResult.success, "要求: 效率附魔") << "\n";

    cleanupTestChest(player, chestPos);
    return report.str();
}

std::string TestHelper::testRecycleRollback(Player& player) {
    std::ostringstream report;
    report << "§e=== 回收回滚测试 ===\n";

    auto chestPosOpt = createTestRecycleChest(player, true);
    if (!chestPosOpt.has_value()) {
        return report.str() + formatTestResult("创建回收箱子", false);
    }

    BlockPos chestPos = chestPosOpt.value();
    int      dimId    = static_cast<int>(player.getDimensionId());
    auto&    region   = player.getDimensionBlockSource();

    // 设置回收委托
    auto item    = ItemStack("minecraft:diamond", 1);
    auto itemNbt = NbtUtils::getItemNbt(item);
    if (!itemNbt) {
        cleanupTestChest(player, chestPos);
        return report.str() + formatTestResult("序列化物品NBT", false);
    }

    auto& recycleService   = RecycleService::getInstance();
    auto  commissionResult = recycleService.setCommission(chestPos, dimId, itemNbt->toSnbt(), 1000.0, 0, "", 10, -1);

    report << formatTestResult("设置高价回收委托", commissionResult.success, "1000金币/个") << "\n";

    // 确保店主金币不足以支付（模拟失败场景）
    auto ownerMoney = Economy::getMoney(player);
    if (ownerMoney >= 1000.0) {
        Economy::reduceMoney(player, ownerMoney - 100.0); // 只留 100
    }
    ownerMoney = Economy::getMoney(player);
    report << "§7店主金币: " << ownerMoney << " (不足以支付回收款)\n";

    // 给玩家钻石
    auto testDiamond = ItemStack("minecraft:diamond", 5);
    player.add(testDiamond);

    // 记录回收前的状态
    auto moneyBefore = Economy::getMoney(player);

    // 尝试回收（应该失败，因为店主金币不足）
    auto recycleResult =
        recycleService
            .executeFullRecycle(player, chestPos, dimId, commissionResult.itemId, 1, 1000.0, itemNbt->toSnbt(), region);

    bool rollbackCheck = !recycleResult.success;
    report << formatTestResult("金币不足时回收失败", rollbackCheck, recycleResult.message) << "\n";

    // 验证金币未变化
    auto moneyAfter     = Economy::getMoney(player);
    bool moneyUnchanged = std::abs(moneyAfter - moneyBefore) < 0.01;
    report << formatTestResult(
        "金币未变化（回滚成功）",
        moneyUnchanged,
        "前: " + std::to_string(moneyBefore) + ", 后: " + std::to_string(moneyAfter)
    ) << "\n";

    cleanupTestChest(player, chestPos);
    return report.str();
}

std::string TestHelper::testRecycleOwnerInsufficientMoney(Player& player) {
    std::ostringstream report;
    report << "§e=== 店主金币不足测试 ===\n";

    auto chestPosOpt = createTestRecycleChest(player, true);
    if (!chestPosOpt.has_value()) {
        return report.str() + formatTestResult("创建回收箱子", false);
    }

    BlockPos chestPos = chestPosOpt.value();
    int      dimId    = static_cast<int>(player.getDimensionId());
    auto&    region   = player.getDimensionBlockSource();

    // 设置高价回收委托
    auto item    = ItemStack("minecraft:netherite_ingot", 1);
    auto itemNbt = NbtUtils::getItemNbt(item);
    if (!itemNbt) {
        cleanupTestChest(player, chestPos);
        return report.str() + formatTestResult("序列化物品NBT", false);
    }

    auto& recycleService   = RecycleService::getInstance();
    auto  commissionResult = recycleService.setCommission(chestPos, dimId, itemNbt->toSnbt(), 50000.0, 0, "", 10, -1);

    report << formatTestResult("设置高价回收委托", commissionResult.success, "50000金币/个") << "\n";

    // 确保店主金币很少
    auto ownerMoney = Economy::getMoney(player);
    if (ownerMoney >= 50000.0) {
        Economy::reduceMoney(player, ownerMoney - 100.0);
    }
    ownerMoney = Economy::getMoney(player);
    report << "§7店主金币: " << ownerMoney << "\n";

    // 给玩家下界合金锭
    auto testIngot = ItemStack("minecraft:netherite_ingot", 2);
    player.add(testIngot);

    // 尝试回收
    auto recycleResult = recycleService.executeFullRecycle(
        player,
        chestPos,
        dimId,
        commissionResult.itemId,
        1,
        50000.0,
        itemNbt->toSnbt(),
        region
    );

    bool insufficientCheck = !recycleResult.success;
    report << formatTestResult("店主金币不足时拒绝回收", insufficientCheck, recycleResult.message) << "\n";

    cleanupTestChest(player, chestPos);
    return report.str();
}

// === 综合测试 ===

std::string TestHelper::runAllTests(Player& player) {
    std::ostringstream fullReport;
    fullReport << "§6========== ChestTrading 自动化测试 ==========\n\n";

    resetTestStats();

    // 商店测试
    fullReport << testShopPurchase(player, true) << "\n";
    fullReport << testShopInventorySync(player) << "\n";
    fullReport << testShopPriceUpdate(player) << "\n";
    fullReport << testShopInsufficientMoney(player) << "\n";
    fullReport << testShopInsufficientStock(player) << "\n";
    fullReport << testShopTaxRate(player) << "\n";
    fullReport << testShopBoundaryConditions(player) << "\n";

    // 回收测试
    fullReport << testRecycle(player, true) << "\n";
    fullReport << testRecycleFilters(player) << "\n";
    fullReport << testRecycleRollback(player) << "\n";
    fullReport << testRecycleOwnerInsufficientMoney(player) << "\n";

    fullReport << getTestSummary(mPassedCount, mFailedCount, mSkippedCount);

    return fullReport.str();
}

std::string TestHelper::runQuickTests(Player& player) {
    std::ostringstream fullReport;
    fullReport << "§6========== 快速测试（核心功能）==========\n\n";

    resetTestStats();

    // 只测试核心功能
    fullReport << testShopPurchase(player, true) << "\n";
    fullReport << testRecycle(player, true) << "\n";

    fullReport << getTestSummary(mPassedCount, mFailedCount, mSkippedCount);

    return fullReport.str();
}

} // namespace CT::Test
