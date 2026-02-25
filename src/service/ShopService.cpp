#include "ShopService.h"
#include "ChestService.h"
#include "Config/ConfigManager.h"
#include "DynamicPricingService.h"
#include "FloatingText/FloatingText.h"
#include "PlayerLimitService.h"
#include "TextService.h"
#include "Utils/MoneyFormat.h"
#include "Utils/ScopeGuard.h"
#include "Utils/economy.h"
#include "db/Sqlite3Wrapper.h"
#include "form/FormUtils.h"
#include "ll/api/service/PlayerInfo.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "repository/ItemRepository.h"
#include "repository/ShopRepository.h"

namespace CT {

ShopService& ShopService::getInstance() {
    static ShopService instance;
    return instance;
}

SetPriceResult ShopService::setItemPrice(
    BlockPos           pos,
    int                dimId,
    const std::string& itemNbt,
    double             price,
    int                initialCount,
    BlockSource&       region
) {
    BlockPos mainPos = ChestService::getInstance().getMainChestPos(pos, region);

    auto& txt = TextService::getInstance();

    // 获取或创建物品ID
    int itemId = ItemRepository::getInstance().getOrCreateItemId(itemNbt);
    if (itemId < 0) {
        return {false, txt.getMessage("shop.item_def_fail"), -1};
    }

    // 创建商品数据
    ShopItemData data;
    data.dimId   = dimId;
    data.pos     = mainPos;
    data.itemId  = itemId;
    data.price   = price;
    data.dbCount = initialCount;
    data.slot    = 0;

    if (!ShopRepository::getInstance().upsertItem(data)) {
        return {false, txt.getMessage("shop.price_set_fail"), -1};
    }

    // 更新悬浮字
    FloatingTextManager::getInstance().updateShopFloatingText(mainPos, dimId, ChestType::Shop);

    return {
        true,
        txt.getMessage(
            "shop.price_set_success",
            {{"price", MoneyFormat::format(price)}, {"count", std::to_string(initialCount)}}
        ),
        itemId
    };
}

bool ShopService::removeItem(BlockPos pos, int dimId, int itemId, BlockSource& region) {
    BlockPos mainPos = ChestService::getInstance().getMainChestPos(pos, region);
    bool     success = ShopRepository::getInstance().removeItem(mainPos, dimId, itemId);
    if (success) {
        FloatingTextManager::getInstance().updateShopFloatingText(mainPos, dimId, ChestType::Shop);
    } else {
        logger.error("删除商店物品失败: itemId={}", itemId);
    }
    return success;
}

std::vector<ShopItemData> ShopService::getShopItems(BlockPos pos, int dimId, BlockSource& region) {
    BlockPos mainPos = ChestService::getInstance().getMainChestPos(pos, region);
    return ShopRepository::getInstance().findAllItems(mainPos, dimId);
}

/**
 * @brief 玩家购买商店物品
 *
 * 这是一个复杂的多步骤事务操作，涉及箱子库存、玩家金钱、数据库记录等多个资源的修改。
 * 为确保事务一致性，使用 ScopeGuard（RAII）模式实现自动回滚机制。
 *
 * @details ScopeGuard 自动回滚机制：
 *
 * **传统错误处理的问题**：
 * - 每个失败点都需要手动撤销之前的所有操作
 * - 容易遗漏回滚步骤，导致数据不一致
 * - 代码重复：每个错误分支都有相同的回滚逻辑
 * - 难以维护：添加新步骤需要更新所有错误分支
 *
 * **ScopeGuard 的优势**：
 * - RAII 自动管理：作用域结束时自动执行回滚
 * - 后进先出（LIFO）：按添加的相反顺序执行回滚
 * - 异常安全：即使抛出异常也能正确回滚
 * - 代码简洁：减少 70% 的错误处理代码
 *
 * @details 购买流程（每步都添加回滚操作）：
 *
 * 1. **验证阶段**（无需回滚）
 *    - 检查商品是否存在
 *    - 检查玩家金钱是否足够
 *    - 检查箱子库存是否充足
 *
 * 2. **物品转移**（需要回滚）
 *    - 从箱子移除物品
 *    - 添加回滚：rollbackGuard.addRollback([&]() { addItemsToChest(...); })
 *    - 失败时自动将物品放回箱子
 *
 * 3. **金钱扣除**（需要回滚）
 *    - 扣除玩家金钱
 *    - 添加回滚：rollbackGuard.addRollback([&]() { Economy::addMoney(...); })
 *    - 失败时自动退还金钱并放回物品
 *
 * 4. **数据库更新**（使用事务）
 *    - 开启数据库事务
 *    - 更新库存计数
 *    - 记录购买记录
 *    - 提交事务
 *    - 失败时数据库自动回滚，ScopeGuard 回滚箱子和金钱
 *
 * 5. **成功确认**
 *    - rollbackGuard.dismiss() - 取消所有回滚操作
 *    - 给店主转账（扣除税率）
 *    - 给买家发放物品
 *
 * @example 错误处理流程
 * ```
 * // 步骤1：移除物品
 * removeItemsFromChest(...);  // 成功
 * rollbackGuard.addRollback([&]() { addItemsToChest(...); });
 *
 * // 步骤2：扣钱
 * if (!Economy::reduceMoney(...)) {
 *     return {false, "..."};  // 返回时 rollbackGuard 析构
 *                             // 自动执行：addItemsToChest()
 * }
 * rollbackGuard.addRollback([&]() { Economy::addMoney(...); });
 *
 * // 步骤3：数据库
 * if (!txn.commit()) {
 *     return {false, "..."};  // 返回时 rollbackGuard 析构
 *                             // 自动执行：Economy::addMoney()
 *                             //         → addItemsToChest()
 *                             // （LIFO 顺序）
 * }
 *
 * // 所有步骤成功
 * rollbackGuard.dismiss();    // 取消回滚
 * ```
 *
 * @param buyer 购买者
 * @param pos 商店箱子位置
 * @param dimId 维度ID
 * @param itemId 物品ID
 * @param quantity 购买数量
 * @param region 方块源
 * @param itemNbt 物品NBT字符串（用于精确匹配）
 *
 * @return PurchaseResult 包含成功标志、消息、实际购买数量和总价
 *
 * @note 线程安全：数据库事务确保并发购买的一致性
 * @note 异常安全：ScopeGuard 即使在异常情况下也能正确回滚
 */
PurchaseResult ShopService::purchaseItem(
    Player&            buyer,
    BlockPos           pos,
    int                dimId,
    int                itemId,
    int                quantity,
    BlockSource&       region,
    const std::string& itemNbt
) {
    auto&    txt     = TextService::getInstance();
    BlockPos mainPos = ChestService::getInstance().getMainChestPos(pos, region);

    // 获取商品信息
    auto itemOpt = ShopRepository::getInstance().findItem(mainPos, dimId, itemId);
    if (!itemOpt) {
        return {false, txt.getMessage("shop.item_not_exist")};
    }

    // 获取箱子信息，判断是否为官方商店
    auto chestInfo   = ChestService::getInstance().getChestInfo(mainPos, dimId, region);
    bool isAdminShop = chestInfo && chestInfo->type == ChestType::AdminShop;

    // 官方商店检查动态价格
    double unitPrice = itemOpt->price;
    if (isAdminShop) {
        auto dpInfo = DynamicPricingService::getInstance().getPriceInfo(mainPos, dimId, itemId, true);
        if (dpInfo) {
            // 检查是否可以交易
            if (!dpInfo->canTrade) {
                return {false, txt.getMessage("dynamic_pricing.sold_out")};
            }
            if (dpInfo->remainingQuantity != -1 && quantity > dpInfo->remainingQuantity) {
                return {
                    false,
                    txt.getMessage(
                        "dynamic_pricing.exceed_limit",
                        {{"remaining", std::to_string(dpInfo->remainingQuantity)}}
                    )
                };
            }
            // 使用动态价格
            unitPrice = dpInfo->currentPrice;
        }
    }

    double totalPrice = quantity * unitPrice;

    // 检查金钱
    if (!Economy::hasMoney(buyer, totalPrice)) {
        return {
            false,
            txt.getMessage("shop.insufficient_money", {{"price", MoneyFormat::format(totalPrice)}}
             )
        };
    }

    // 检查限购
    auto limitCheck = PlayerLimitService::getInstance().checkLimit(
        mainPos,
        dimId,
        buyer.getUuid().asString(),
        quantity,
        true,
        itemId
    );
    if (!limitCheck.allowed) {
        return {false, limitCheck.message};
    }

    // 获取箱子实体
    auto* chest = getChestActor(region, mainPos);
    if (!chest) {
        return {false, txt.getMessage("shop.purchase_chest_fail")};
    }

    // 使用 ScopeGuard 自动管理回滚操作（RAII）
    ScopeGuard rollbackGuard;

    // 官方商店：无限出售，不检查库存，不移除物品
    if (!isAdminShop) {
        // 检查箱子库存
        int actualAvailable = countMatchingItems(chest, itemNbt);
        if (actualAvailable < quantity) {
            return {
                false,
                txt.getMessage("shop.insufficient_stock", {{"stock", std::to_string(actualAvailable)}}
                 )
            };
        }

        // 从箱子移除物品
        int actualRemoved = removeItemsFromChest(chest, itemNbt, quantity);
        if (actualRemoved < quantity) {
            // 放回已移除的物品
            if (actualRemoved > 0) {
                addItemsToChest(chest, itemNbt, actualRemoved);
            }
            return {false, txt.getMessage("shop.purchase_chest_fail")};
        }
        // 添加回滚操作：放回物品
        rollbackGuard.addRollback([chest, itemNbt, quantity]() { addItemsToChest(chest, itemNbt, quantity); });
    }

    // 扣钱（金额为0时跳过）
    if (totalPrice > 0) {
        if (!Economy::reduceMoney(buyer, totalPrice)) {
            return {false, txt.getMessage("shop.purchase_money_fail")};
            // rollbackGuard 析构时会自动放回物品
        }
        // 添加回滚操作：退钱
        rollbackGuard.addRollback([&buyer, totalPrice]() { Economy::addMoney(buyer, totalPrice); });
    }

    // 使用事务更新数据库库存和记录购买
    auto&       db = Sqlite3Wrapper::getInstance();
    Transaction txn(db);
    if (!txn.isActive()) {
        return {false, txt.getMessage("shop.purchase_db_fail")};
        // rollbackGuard 析构时会自动退钱和放回物品
    }

    // 官方商店不更新库存
    if (!isAdminShop) {
        // 更新数据库库存
        if (!ShopRepository::getInstance().decrementDbCount(mainPos, dimId, itemId, quantity)) {
            txn.rollback();
            return {false, txt.getMessage("shop.purchase_db_fail")};
            // rollbackGuard 析构时会自动退钱和放回物品
        }
    }

    // 记录购买
    PurchaseRecordData record;
    record.dimId         = dimId;
    record.pos           = mainPos;
    record.itemId        = itemId;
    record.buyerUuid     = buyer.getUuid().asString();
    record.purchaseCount = quantity;
    record.totalPrice    = totalPrice;
    if (!ShopRepository::getInstance().addPurchaseRecord(record)) {
        txn.rollback();
        return {false, txt.getMessage("shop.purchase_db_fail")};
        // rollbackGuard 析构时会自动退钱和放回物品
    }

    // 提交事务
    if (!txn.commit()) {
        return {false, txt.getMessage("shop.purchase_db_fail")};
        // rollbackGuard 析构时会自动退钱和放回物品
    }

    // 事务成功提交，取消回滚操作
    rollbackGuard.dismiss();

    // 官方商店记录动态价格交易量
    if (isAdminShop) {
        DynamicPricingService::getInstance().recordTrade(mainPos, dimId, itemId, true, quantity);
    }

    // 官方商店不给店主加钱
    if (!isAdminShop && chestInfo && !chestInfo->ownerUuid.empty()) {
        double taxRate     = ConfigManager::getInstance().get().taxSettings.shopTaxRate;
        double ownerIncome = totalPrice * (1.0 - taxRate);
        Economy::addMoneyByUuid(chestInfo->ownerUuid, ownerIncome);
    }

    // 给玩家物品
    auto itemPtr = FormUtils::createItemStackFromNbtString(itemNbt);
    if (itemPtr) {
        int maxStackSize    = itemPtr->getMaxStackSize();
        int remainingToGive = quantity;
        while (remainingToGive > 0) {
            int       giveCount  = std::min(remainingToGive, maxStackSize);
            ItemStack itemToGive = *itemPtr;
            itemToGive.set(giveCount);
            if (!buyer.add(itemToGive)) {
                buyer.drop(itemToGive, true);
            }
            remainingToGive -= giveCount;
        }
        buyer.refreshInventory();
    }

    // 更新悬浮字
    ChestType shopType = isAdminShop ? ChestType::AdminShop : ChestType::Shop;
    FloatingTextManager::getInstance().updateShopFloatingText(mainPos, dimId, shopType);

    std::string itemName = itemPtr ? std::string(itemPtr->getName()) : txt.getMessage("shop.unknown_item");
    return {
        true,
        txt.getMessage(
            "shop.purchase_success",
            {{"price", MoneyFormat::format(totalPrice)}, {"item", itemName}, {"count", std::to_string(quantity)}}
        ),
        quantity,
        totalPrice
    };
}

int ShopService::countItemsInChest(BlockSource& region, BlockPos pos, int dimId, const std::string& itemNbt) {
    auto* chest = getChestActor(region, pos);
    return countMatchingItems(chest, itemNbt);
}

} // namespace CT
