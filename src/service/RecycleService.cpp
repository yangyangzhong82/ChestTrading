#include "RecycleService.h"
#include "ChestService.h"
#include "Config/ConfigManager.h"
#include "DynamicPricingService.h"
#include "FloatingText/FloatingText.h"
#include "PlayerLimitService.h"
#include "TextService.h"
#include "Utils/NbtUtils.h"
#include "Utils/ScopeGuard.h"
#include "Utils/economy.h"
#include "db/Sqlite3Wrapper.h"
#include "ll/api/service/PlayerInfo.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "nlohmann/json.hpp"
#include "repository/ItemRepository.h"
#include "repository/ShopRepository.h"
#include <algorithm>

namespace CT {

RecycleService& RecycleService::getInstance() {
    static RecycleService instance;
    return instance;
}

SetCommissionResult RecycleService::setCommission(
    BlockPos           pos,
    int                dimId,
    const std::string& itemNbt,
    double             price,
    int                minDurability,
    const std::string& requiredEnchants,
    int                maxRecycleCount,
    int                requiredAuxValue
) {
    auto& itemRepo = ItemRepository::getInstance();
    auto& txt      = TextService::getInstance();
    int   itemId   = itemRepo.getOrCreateItemId(itemNbt);
    if (itemId < 0) {
        return {false, txt.getMessage("shop.item_def_fail"), -1};
    }

    RecycleItemData data;
    data.dimId            = dimId;
    data.pos              = pos;
    data.itemId           = itemId;
    data.price            = price;
    data.minDurability    = minDurability;
    data.requiredEnchants = requiredEnchants;
    data.maxRecycleCount  = maxRecycleCount;
    data.requiredAuxValue = requiredAuxValue;

    auto& shopRepo = ShopRepository::getInstance();
    if (shopRepo.upsertRecycleItem(data)) {
        FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, ChestType::RecycleShop);
        return {true, "", itemId};
    }
    return {false, txt.getMessage("recycle.db_fail"), -1};
}

bool RecycleService::updateCommission(BlockPos pos, int dimId, int itemId, double price, int maxRecycleCount) {
    auto& shopRepo = ShopRepository::getInstance();
    if (shopRepo.updateRecycleItem(pos, dimId, itemId, price, maxRecycleCount)) {
        FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, ChestType::RecycleShop);
        return true;
    }
    logger.error("更新回收委托失败: itemId={}, price={}", itemId, price);
    return false;
}

std::vector<RecycleItemData> RecycleService::getCommissions(BlockPos pos, int dimId) {
    return ShopRepository::getInstance().findAllRecycleItems(pos, dimId);
}

std::optional<RecycleItemData> RecycleService::getCommission(BlockPos pos, int dimId, int itemId) {
    return ShopRepository::getInstance().findRecycleItem(pos, dimId, itemId);
}

std::vector<RecycleRecordData> RecycleService::getRecycleRecords(BlockPos pos, int dimId, int itemId, int limit) {
    return ShopRepository::getInstance().getRecycleRecords(pos, dimId, itemId, limit);
}

bool RecycleService::executeDbUpdate(
    Player&  recycler,
    BlockPos pos,
    int      dimId,
    int      itemId,
    int      quantity,
    double   totalPrice
) {
    auto& shopRepo = ShopRepository::getInstance();
    auto& db       = Sqlite3Wrapper::getInstance();

    Transaction txn(db);
    if (!txn.isActive()) {
        logger.error("回收数据库更新失败: 无法开始事务");
        return false;
    }

    if (!shopRepo.incrementRecycledCount(pos, dimId, itemId, quantity)) {
        logger.error("回收数据库更新失败: 无法更新回收计数, itemId={}, quantity={}", itemId, quantity);
        return false;
    }

    RecycleRecordData record;
    record.dimId        = dimId;
    record.pos          = pos;
    record.itemId       = itemId;
    record.recyclerUuid = recycler.getUuid().asString();
    record.recycleCount = quantity;
    record.totalPrice   = totalPrice;

    if (!shopRepo.addRecycleRecord(record)) {
        logger.error("回收数据库更新失败: 无法添加回收记录, itemId={}", itemId);
        return false;
    }

    if (!txn.commit()) {
        logger.error("回收数据库更新失败: 无法提交事务");
        return false;
    }
    return true;
}

RecycleResult RecycleService::executeFullRecycle(
    Player&            recycler,
    BlockPos           pos,
    int                dimId,
    int                itemId,
    int                quantity,
    double             unitPrice,
    const std::string& commissionNbtStr,
    BlockSource&       region
) {
    auto& txt      = TextService::getInstance();
    auto& shopRepo = ShopRepository::getInstance();
    BlockPos mainPos = ChestService::getInstance().getMainChestPos(pos, region);

    // 1. 获取箱子信息
    auto chestInfo = ChestService::getInstance().getChestInfo(mainPos, dimId, region);
    if (!chestInfo) {
        return {false, txt.getMessage("recycle.not_recycle_shop"), 0, 0.0};
    }
    std::string ownerUuid      = chestInfo->ownerUuid;
    bool        isAdminRecycle = chestInfo->type == ChestType::AdminRecycle;

    // 官方回收商店检查动态价格
    double actualUnitPrice = unitPrice;
    if (isAdminRecycle) {
        auto dpInfo = DynamicPricingService::getInstance().getPriceInfo(mainPos, dimId, itemId, false);
        if (dpInfo) {
            // 检查是否可以交易
            if (!dpInfo->canTrade) {
                return {false, txt.getMessage("dynamic_pricing.recycle_stopped"), 0, 0.0};
            }
            if (dpInfo->remainingQuantity != -1 && quantity > dpInfo->remainingQuantity) {
                return {
                    false,
                    txt.getMessage(
                        "dynamic_pricing.recycle_exceed_limit",
                        {{"remaining", std::to_string(dpInfo->remainingQuantity)}}
                    ),
                    0,
                    0.0
                };
            }
            // 使用动态价格
            actualUnitPrice = dpInfo->currentPrice;
        }
    }

    double totalPrice = actualUnitPrice * quantity;

    // 2. 普通回收商店：先原子扣除店主金钱（解决竞态条件）
    bool ownerMoneyDeducted = false;
    if (!isAdminRecycle) {
        if (!Economy::reduceMoneyByUuid(ownerUuid, totalPrice)) {
            return {false, txt.getMessage("recycle.owner_insufficient"), 0, 0.0};
        }
        ownerMoneyDeducted = true;
    }

    // 辅助lambda：退还店主金钱
    auto refundOwner = [&]() {
        if (ownerMoneyDeducted) {
            if (!Economy::addMoneyByUuid(ownerUuid, totalPrice)) {
                logger.error("退还店主金钱失败: ownerUuid={}, amount={}", ownerUuid, totalPrice);
            }
            ownerMoneyDeducted = false; // 防止重复退款
        }
    };

    // 3. 获取箱子实体
    auto* chest = getChestActor(region, mainPos);
    if (!chest) {
        refundOwner();
        return {false, txt.getMessage("recycle.chest_entity_fail"), 0, 0.0};
    }

    // 4. 获取委托信息
    auto commission = shopRepo.findRecycleItem(mainPos, dimId, itemId);
    if (!commission) {
        refundOwner();
        return {false, txt.getMessage("recycle.no_commission"), 0, 0.0};
    }

    int            minDurability        = commission->minDurability;
    int            maxRecycleCount      = commission->maxRecycleCount;
    int            currentRecycledCount = commission->currentRecycledCount;
    int            requiredAuxValue     = commission->requiredAuxValue;
    nlohmann::json requiredEnchants;
    if (!commission->requiredEnchants.empty()) {
        try {
            requiredEnchants = nlohmann::json::parse(commission->requiredEnchants);
        } catch (...) {}
    }

    // 5. 检查回收数量限制（官方回收商店可设置无限回收，maxRecycleCount=0表示无限）
    if (maxRecycleCount > 0 && (currentRecycledCount + quantity) > maxRecycleCount) {
        refundOwner();
        return {
            false,
            txt.getMessage("recycle.max_reached", {{"max", std::to_string(maxRecycleCount)}}
             ),
            0,
            0.0
        };
    }

    // 5.5 检查玩家限购
    auto limitCheck =
        PlayerLimitService::getInstance().checkLimit(mainPos, dimId, recycler.getUuid().asString(), quantity, false);
    if (!limitCheck.allowed) {
        refundOwner();
        return {false, limitCheck.message, 0, 0.0};
    }

    // 6. 查找并记录要转移的物品（精确记录槽位和数量）
    auto&                       playerInventory = recycler.getInventory();
    std::vector<TransferRecord> transferRecords;
    int                         foundCount = 0;

    for (int i = 0; i < playerInventory.getContainerSize() && foundCount < quantity; ++i) {
        const auto& itemInSlot = playerInventory.getItem(i);
        if (itemInSlot.isNull()) continue;

        auto itemNbt = NbtUtils::getItemNbt(itemInSlot);
        if (!itemNbt) continue;
        auto cleanedNbt = NbtUtils::cleanNbtForComparison(*itemNbt);

        if (NbtUtils::toSNBT(*cleanedNbt) == commissionNbtStr) {
            // 检查特殊值（如箭的类型）
            if (requiredAuxValue >= 0 && itemInSlot.getAuxValue() != requiredAuxValue) {
                continue;
            }
            // 检查耐久度
            if (itemInSlot.isDamageableItem()) {
                int maxDamage     = itemInSlot.getItem()->getMaxDamage();
                int currentDamage = itemInSlot.getDamageValue();
                if ((maxDamage - currentDamage) < minDurability) continue;
            }
            // 检查附魔
            if (!requiredEnchants.empty() && requiredEnchants.is_array()) {
                bool         allEnchantsMatch = true;
                ItemEnchants itemEnchants     = itemInSlot.constructItemEnchantsFromUserData();
                auto         allItemEnchants  = itemEnchants.getAllEnchants();
                for (const auto& reqEnchant : requiredEnchants) {
                    bool found    = false;
                    int  reqId    = reqEnchant["id"];
                    int  reqLevel = reqEnchant["level"];
                    for (const auto& ie : allItemEnchants) {
                        if ((int)ie.mEnchantType == reqId && ie.mLevel >= reqLevel) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        allEnchantsMatch = false;
                        break;
                    }
                }
                if (!allEnchantsMatch) continue;
            }

            int countCanRemove = std::min((int)itemInSlot.mCount, quantity - foundCount);
            transferRecords.push_back({i, countCanRemove});
            foundCount += countCanRemove;
        }
    }

    if (foundCount < quantity) {
        refundOwner();
        return {false, txt.getMessage("recycle.item_insufficient"), 0, 0.0};
    }

    // 7. 官方回收商店不往箱子添加物品，普通回收商店需要检查空间并添加
    std::map<int, int> chestInitialCounts;
    if (!isAdminRecycle) {
        // 检查箱子空间
        int chestAvailableSpace = 0;
        for (int i = 0; i < chest->getContainerSize(); ++i) {
            const auto& chestItem = chest->getItem(i);
            if (chestItem.isNull()) {
                chestAvailableSpace += 64;//空槽位
            } else {
                auto chestItemNbt = NbtUtils::getItemNbt(chestItem);
                if (chestItemNbt) {
                    auto cleanedChestNbt = NbtUtils::cleanNbtForComparison(*chestItemNbt);
                    if (NbtUtils::toSNBT(*cleanedChestNbt) == commissionNbtStr) {
                        chestAvailableSpace += (64 - chestItem.mCount);
                    }
                }
            }
        }
        if (chestAvailableSpace < quantity) {
            refundOwner();
            return {false, txt.getMessage("recycle.chest_full"), 0, 0.0};
        }

        // 记录箱子初始状态（每个槽位的物品数量）
        for (int i = 0; i < chest->getContainerSize(); ++i) {
            const auto& chestItem = chest->getItem(i);
            if (!chestItem.isNull()) {
                auto chestItemNbt = NbtUtils::getItemNbt(chestItem);
                if (chestItemNbt) {
                    auto cleanedNbt = NbtUtils::cleanNbtForComparison(*chestItemNbt);
                    if (NbtUtils::toSNBT(*cleanedNbt) == commissionNbtStr) {
                        chestInitialCounts[i] = chestItem.mCount;
                    }
                }
            }
        }
    }

    // 使用 ScopeGuard 自动管理回滚操作（RAII）
    ScopeGuard rollbackGuard;

    // 8. 官方回收商店：保存原始物品副本用于回滚
    std::vector<ItemStack> originalItemsForRollback;
    if (isAdminRecycle) {
        for (const auto& tr : transferRecords) {
            ItemStack copy = playerInventory.getItem(tr.slot);
            copy.setStackSize(tr.count);
            originalItemsForRollback.push_back(std::move(copy));
        }
    }

    // 9. 执行物品转移
    for (const auto& tr : transferRecords) {
        if (!isAdminRecycle) {
            // 普通回收商店：物品转移到箱子
            const auto& originalItem  = playerInventory.getItem(tr.slot);
            ItemStack   itemToRecycle = originalItem;
            itemToRecycle.setStackSize(tr.count);

            if (!chest->addItem(itemToRecycle)) {
                // 转移失败，精确回滚：只移除本次新增的部分
                for (int i = 0; i < chest->getContainerSize(); ++i) {
                    const auto& chestItem = chest->getItem(i);
                    if (chestItem.isNull()) continue;
                    auto chestItemNbt = NbtUtils::getItemNbt(chestItem);
                    if (!chestItemNbt) continue;
                    auto cleanedNbt = NbtUtils::cleanNbtForComparison(*chestItemNbt);
                    if (NbtUtils::toSNBT(*cleanedNbt) != commissionNbtStr) continue;
                    int addedCount = chestItem.mCount - (chestInitialCounts.count(i) ? chestInitialCounts[i] : 0);
                    if (addedCount > 0) {
                        ItemStack returnItem = chestItem;
                        returnItem.setStackSize(addedCount);
                        chest->removeItem(i, addedCount);
                        if (!recycler.add(returnItem)) recycler.drop(returnItem, true);
                    }
                }
                refundOwner();
                recycler.refreshInventory();
                return {false, txt.getMessage("recycle.chest_full"), 0, 0.0};
            }
        }
        // 从玩家背包移除物品（官方和普通都需要）
        playerInventory.removeItem(tr.slot, tr.count);
    }

    // 添加回滚操作：将物品从箱子移回玩家背包
    if (!isAdminRecycle) {
        rollbackGuard.addRollback([&]() {
            // 从箱子移除本次新增的物品
            for (int i = 0; i < chest->getContainerSize(); ++i) {
                const auto& chestItem = chest->getItem(i);
                if (chestItem.isNull()) continue;
                auto chestItemNbt = NbtUtils::getItemNbt(chestItem);
                if (!chestItemNbt) continue;
                auto cleanedNbt = NbtUtils::cleanNbtForComparison(*chestItemNbt);
                if (NbtUtils::toSNBT(*cleanedNbt) != commissionNbtStr) continue;
                int addedCount = chestItem.mCount - (chestInitialCounts.count(i) ? chestInitialCounts[i] : 0);
                if (addedCount > 0) {
                    ItemStack returnItem = chestItem;
                    returnItem.setStackSize(addedCount);
                    chest->removeItem(i, addedCount);
                    if (!recycler.add(returnItem)) recycler.drop(returnItem, true);
                }
            }
            refundOwner();
            recycler.refreshInventory();
        });
    } else {
        // 官方回收商店：使用保存的原始物品副本进行回滚（保留完整属性）
        rollbackGuard.addRollback([&originalItemsForRollback, &recycler]() {
            for (auto& item : originalItemsForRollback) {
                if (!recycler.add(item)) recycler.drop(item, true);
            }
            recycler.refreshInventory();
        });
    }

    // 10. 给回收者加钱（扣除税率）
    double taxRate      = ConfigManager::getInstance().get().taxSettings.recycleTaxRate;
    double recyclerGain = totalPrice * (1.0 - taxRate);
    Economy::addMoney(recycler, recyclerGain);

    // 添加回滚操作：扣除回收者金钱（LIFO 顺序：先回滚金钱，再回滚物品）
    rollbackGuard.addRollback([&]() { Economy::reduceMoney(recycler, recyclerGain); });

    // 11. 更新数据库（事务）
    if (!executeDbUpdate(recycler, mainPos, dimId, itemId, quantity, totalPrice)) {
        return {false, txt.getMessage("recycle.db_fail"), 0, 0.0};
        // rollbackGuard 析构时会自动回滚金钱和物品
    }

    // 事务成功提交，取消回滚操作
    rollbackGuard.dismiss();

    // 官方回收商店记录动态价格交易量
    if (isAdminRecycle) {
        DynamicPricingService::getInstance().recordTrade(mainPos, dimId, itemId, false, quantity);
    }

    recycler.refreshInventory();
    return {true, "", quantity, totalPrice};
}

} // namespace CT
