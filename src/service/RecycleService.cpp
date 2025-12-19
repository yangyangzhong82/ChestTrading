#include "RecycleService.h"
#include "ChestService.h"
#include "Config/ConfigManager.h"
#include "FloatingText/FloatingText.h"
#include "TextService.h"
#include "Utils/NbtUtils.h"
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
    int                maxRecycleCount
) {
    auto& itemRepo = ItemRepository::getInstance();
    int   itemId   = itemRepo.getOrCreateItemId(itemNbt);
    if (itemId < 0) {
        return {false, "无法创建物品定义", -1};
    }

    RecycleItemData data;
    data.dimId            = dimId;
    data.pos              = pos;
    data.itemId           = itemId;
    data.price            = price;
    data.minDurability    = minDurability;
    data.requiredEnchants = requiredEnchants;
    data.maxRecycleCount  = maxRecycleCount;

    auto& shopRepo = ShopRepository::getInstance();
    if (shopRepo.upsertRecycleItem(data)) {
        FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, ChestType::RecycleShop);
        return {true, "", itemId};
    }
    return {false, "数据库操作失败", -1};
}

bool RecycleService::updateCommission(BlockPos pos, int dimId, int itemId, double price, int maxRecycleCount) {
    auto& shopRepo = ShopRepository::getInstance();
    if (shopRepo.updateRecycleItem(pos, dimId, itemId, price, maxRecycleCount)) {
        FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, ChestType::RecycleShop);
        return true;
    }
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
        return false;
    }

    if (!shopRepo.incrementRecycledCount(pos, dimId, itemId, quantity)) {
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
        return false;
    }

    return txn.commit();
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

    // 1. 获取箱子信息
    auto chestInfo = ChestService::getInstance().getChestInfo(pos, dimId, region);
    if (!chestInfo) {
        return {false, txt.getMessage("recycle.not_recycle_shop"), 0, 0.0};
    }
    std::string ownerUuid = chestInfo->ownerUuid;

    // 2. 获取店主信息并检查余额
    auto ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(ownerUuid));
    if (!ownerInfo) {
        return {false, txt.getMessage("recycle.owner_not_found"), 0, 0.0};
    }

    double totalPrice = unitPrice * quantity;
    if (Economy::getMoney(ownerInfo->xuid) < totalPrice) {
        return {false, txt.getMessage("recycle.owner_insufficient"), 0, 0.0};
    }

    // 3. 获取箱子实体
    auto* blockActor = region.getBlockEntity(pos);
    if (!blockActor || blockActor->mType != BlockActorType::Chest) {
        return {false, txt.getMessage("recycle.chest_entity_fail"), 0, 0.0};
    }
    auto* chest = reinterpret_cast<ChestBlockActor*>(blockActor);

    // 4. 获取委托信息
    auto commission = shopRepo.findRecycleItem(pos, dimId, itemId);
    if (!commission) {
        return {false, txt.getMessage("recycle.no_commission"), 0, 0.0};
    }

    int            minDurability        = commission->minDurability;
    int            maxRecycleCount      = commission->maxRecycleCount;
    int            currentRecycledCount = commission->currentRecycledCount;
    nlohmann::json requiredEnchants;
    if (!commission->requiredEnchants.empty()) {
        try {
            requiredEnchants = nlohmann::json::parse(commission->requiredEnchants);
        } catch (...) {}
    }

    // 5. 检查回收数量限制
    if (maxRecycleCount > 0 && (currentRecycledCount + quantity) > maxRecycleCount) {
        return {
            false,
            txt.getMessage("recycle.max_reached", {{"max", std::to_string(maxRecycleCount)}}
             ),
            0,
            0.0
        };
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
        return {false, txt.getMessage("recycle.item_insufficient"), 0, 0.0};
    }

    // 7. 检查箱子空间
    int chestAvailableSpace = 0;
    for (int i = 0; i < chest->getContainerSize(); ++i) {
        const auto& chestItem = chest->getItem(i);
        if (chestItem.isNull()) {
            chestAvailableSpace += 64; // 假设最大堆叠
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
        return {false, txt.getMessage("recycle.chest_full"), 0, 0.0};
    }

    // 8. 执行物品转移（记录实际转移的槽位用于回滚）
    std::vector<TransferRecord> actualTransfers;
    for (const auto& tr : transferRecords) {
        const auto& originalItem  = playerInventory.getItem(tr.slot);
        ItemStack   itemToRecycle = originalItem;
        itemToRecycle.setStackSize(tr.count);

        if (!chest->addItem(itemToRecycle)) {
            // 转移失败，回滚已转移的物品
            for (const auto& at : actualTransfers) {
                // 从箱子中找回并还给玩家
                for (int i = 0; i < chest->getContainerSize(); ++i) {
                    const auto& chestItem = chest->getItem(i);
                    if (!chestItem.isNull()) {
                        auto chestItemNbt = NbtUtils::getItemNbt(chestItem);
                        if (chestItemNbt) {
                            auto cleanedNbt = NbtUtils::cleanNbtForComparison(*chestItemNbt);
                            if (NbtUtils::toSNBT(*cleanedNbt) == commissionNbtStr) {
                                int       removeCount = std::min(at.count, (int)chestItem.mCount);
                                ItemStack returnItem  = chestItem;
                                returnItem.setStackSize(removeCount);
                                chest->removeItem(i, removeCount);
                                if (!recycler.add(returnItem)) {
                                    recycler.drop(returnItem, true);
                                }
                                break;
                            }
                        }
                    }
                }
            }
            recycler.refreshInventory();
            return {false, txt.getMessage("recycle.chest_full"), 0, 0.0};
        }
        playerInventory.removeItem(tr.slot, tr.count);
        actualTransfers.push_back(tr);
    }

    // 9. 扣店主钱
    if (!Economy::reduceMoneyByUuid(ownerUuid, totalPrice)) {
        // 扣款失败，精确回滚：按记录的槽位和数量从箱子取回
        int remaining = quantity;
        for (int i = 0; i < chest->getContainerSize() && remaining > 0; ++i) {
            const auto& chestItem = chest->getItem(i);
            if (!chestItem.isNull()) {
                auto chestItemNbt = NbtUtils::getItemNbt(chestItem);
                if (chestItemNbt) {
                    auto cleanedNbt = NbtUtils::cleanNbtForComparison(*chestItemNbt);
                    if (NbtUtils::toSNBT(*cleanedNbt) == commissionNbtStr) {
                        int       removeCount = std::min(remaining, (int)chestItem.mCount);
                        ItemStack returnItem  = chestItem;
                        returnItem.setStackSize(removeCount);
                        chest->removeItem(i, removeCount);
                        if (!recycler.add(returnItem)) {
                            recycler.drop(returnItem, true);
                        }
                        remaining -= removeCount;
                    }
                }
            }
        }
        recycler.refreshInventory();
        return {false, txt.getMessage("recycle.money_refund"), 0, 0.0};
    }

    // 10. 给回收者加钱（扣除税率）
    double taxRate      = ConfigManager::getInstance().get().taxSettings.recycleTaxRate;
    double recyclerGain = totalPrice * (1.0 - taxRate);
    Economy::addMoney(recycler, recyclerGain);

    // 11. 更新数据库（事务）
    if (!executeDbUpdate(recycler, pos, dimId, itemId, quantity, totalPrice)) {
        // 数据库更新失败，但金钱和物品已转移，记录错误但不回滚（避免复杂性）
        logger.error(
            "Recycle DB update failed but transaction completed. Player: {}, ItemId: {}, Count: {}",
            recycler.getRealName(),
            itemId,
            quantity
        );
    }

    recycler.refreshInventory();
    return {true, "", quantity, totalPrice};
}

} // namespace CT