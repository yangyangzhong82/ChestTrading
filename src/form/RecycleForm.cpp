#include "RecycleForm.h"
#include "LLMoney.h"
#include "LockForm.h"
#include "Utils/ItemTextureManager.h"
#include "Utils/NbtUtils.h"
#include "Utils/economy.h"
#include "db/Sqlite3Wrapper.h"
#include "interaction/chestprotect.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/PlayerInfo.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "nlohmann/json.hpp"


namespace CT {

using CT::NbtUtils::enchantToString;

void showRecycleForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    showRecycleItemListForm(player, pos, dimId, region);
}

void showRecycleItemListForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    fm.setTitle("回收商店 - 物品列表");
    fm.setContent("选择你想要回收的物品：");

    // 1. 获取此回收箱的所有回收委托
    auto& db          = Sqlite3Wrapper::getInstance();
    auto  commissions = db.query(
        "SELECT item_nbt, price, min_durability, required_enchants FROM recycle_shop_items "
        "WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    if (commissions.empty()) {
        fm.setContent("该回收商店没有任何回收委托。");
    } else {
        fm.setContent("以下是该回收商店的所有回收委托：");
        for (const auto& row : commissions) {
            std::string commissionNbtStr  = row[0];
            int         price             = std::stoi(row[1]);
            int         minDurability     = std::stoi(row[2]);
            std::string requiredEnchantsStr = row[3];

            auto itemNbt = CT::NbtUtils::parseSNBT(commissionNbtStr);
            if (!itemNbt) {
                fm.appendButton("§c[数据损坏] 无法加载委托物品 (NBT解析失败)", [](Player& p) {
                    p.sendMessage("§c该委托物品数据已损坏，无法回收。");
                });
                continue;
            }
            itemNbt->at("Count") = ByteTag(1); // 从NBT创建物品需要Count标签
            auto itemPtr         = CT::NbtUtils::createItemFromNbt(*itemNbt);
            if (!itemPtr) {
                fm.appendButton("§c[数据损坏] 无法加载委托物品 (创建失败)", [](Player& p) {
                    p.sendMessage("§c该委托物品数据已损坏，无法回收。");
                });
                continue;
            }
            ItemStack item = *itemPtr;
            item.mCount    = 1; // 确保 item 的数量为 1，用于显示

            std::string buttonText = std::string(item.getName()) + " §7(" + item.getTypeName() + ")§r"
                                   + " §6[回收单价: " + std::to_string(price) + "]§r";

            std::string itemInfo;
            if (minDurability > 0) {
                itemInfo += "\n§a最低耐久: " + std::to_string(minDurability) + "§r";
            }
            if (!requiredEnchantsStr.empty()) {
                try {
                    nlohmann::json enchants = nlohmann::json::parse(requiredEnchantsStr);
                    if (enchants.is_array() && !enchants.empty()) {
                        itemInfo += "\n§d要求附魔: ";
                        for (const auto& enchant : enchants) {
                            int id    = enchant["id"];
                            int level = enchant["level"];
                            itemInfo += enchantToString((Enchant::Type)id) + " " + std::to_string(level) + " ";
                        }
                        itemInfo += "§r";
                    }
                } catch (const std::exception& e) {
                    logger.error("Failed to parse required_enchants JSON: {}", e.what());
                }
            }
            buttonText += itemInfo;

            std::string itemName = item.getTypeName();
            if (itemName.rfind("minecraft:", 0) == 0) {
                itemName = itemName.substr(10);
            }
            std::string texturePath = CT::ItemTextureManager::getInstance().getTexture(itemName);

            if (!texturePath.empty()) {
                fm.appendButton(
                    buttonText,
                    texturePath,
                    "path",
                    [&player, pos, dimId, &region, item, price, commissionNbtStr](Player& p) {
                        showRecycleConfirmForm(p, item, pos, dimId, region, -1, price, commissionNbtStr);
                    }
                );
            } else {
                fm.appendButton(buttonText, [&player, pos, dimId, &region, item, price, commissionNbtStr](Player& p) {
                    showRecycleConfirmForm(p, item, pos, dimId, region, -1, price, commissionNbtStr);
                });
            }
        }
    }

    fm.appendButton("返回", [&player, pos, dimId, &region](Player& p) {
        // 返回到箱子已锁定界面
        std::string   ownerUuid = player.getUuid().asString();
        bool          isLocked  = true;
        CT::ChestType chestType = CT::ChestType::RecycleShop;
        CT::showChestLockForm(p, pos, dimId, isLocked, ownerUuid, chestType, region);
    });
    fm.sendTo(player);
}

void showRecycleFinalConfirmForm(
    Player&            player,
    const ItemStack&   item,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    int                recycleCount,
    long long          recyclePrice,
    const std::string& commissionNbtStr,
    int                unitPrice
);

void showRecycleConfirmForm(
    Player&            player,
    const ItemStack&   item,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    int                actualSlotIndex, // This will be -1 when called from the commission list
    int                unitPrice,
    const std::string& commissionNbtStr
) {
    ll::form::CustomForm fm;
    fm.setTitle("确认回收物品");

    // 查找玩家背包中所有可回收的此种物品
    int   totalPlayerCount = 0;
    auto& db               = Sqlite3Wrapper::getInstance();
    auto  commission       = db.query(
        "SELECT min_durability, required_enchants FROM recycle_shop_items WHERE dim_id = ? "
        "AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_nbt = ?",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        commissionNbtStr
    );

    if (commission.empty()) {
        player.sendMessage("§c无法找到此回收委托。");
        return;
    }

    int         minDurability     = std::stoi(commission[0][0]);
    std::string requiredEnchantsStr = commission[0][1];
    nlohmann::json requiredEnchants;
    if (!requiredEnchantsStr.empty()) {
        try {
            requiredEnchants = nlohmann::json::parse(requiredEnchantsStr);
        } catch (const std::exception& e) {
            logger.error("Failed to parse required_enchants JSON in showRecycleConfirmForm: {}", e.what());
        }
    }


    for (int i = 0; i < player.getInventory().getContainerSize(); ++i) {
        const auto& itemInSlot = player.getInventory().getItem(i);
        if (itemInSlot.isNull()) continue;

        auto itemNbt = CT::NbtUtils::getItemNbt(itemInSlot);
        if (!itemNbt) continue;
        auto itemNbtForComparison = itemNbt->clone();
        if (itemNbtForComparison->contains("Count")) {
            itemNbtForComparison->erase("Count");
        }
        std::string itemNbtStr = CT::NbtUtils::toSNBT(*itemNbtForComparison);

        if (itemNbtStr == commissionNbtStr) {
            // 检查耐久度
            if (itemInSlot.isDamageableItem()) {
                int maxDamage         = itemInSlot.getItem()->getMaxDamage();
                int currentDamage     = itemInSlot.getDamageValue();
                int currentDurability = maxDamage - currentDamage;
                if (currentDurability < minDurability) continue;
            }
            // 检查附魔
            if (!requiredEnchants.empty() && requiredEnchants.is_array()) {
                bool allEnchantsMatch = true;
                ItemEnchants itemEnchants = itemInSlot.constructItemEnchantsFromUserData();
                auto allItemEnchants = itemEnchants.getAllEnchants();

                for (const auto& reqEnchant : requiredEnchants) {
                    bool currentEnchantFound = false;
                    int reqId = reqEnchant["id"];
                    int reqLevel = reqEnchant["level"];
                    for (const auto& itemEnchant : allItemEnchants) {
                        if ((int)itemEnchant.mEnchantType == reqId && itemEnchant.mLevel >= reqLevel) {
                            currentEnchantFound = true;
                            break;
                        }
                    }
                    if (!currentEnchantFound) {
                        allEnchantsMatch = false;
                        break;
                    }
                }
                if (!allEnchantsMatch) continue;
            }
            totalPlayerCount += itemInSlot.mCount;
        }
    }

    fm.appendLabel("你正在回收物品: " + std::string(item.getName()) + " §7(" + item.getTypeName() + ")§r");
    fm.appendLabel("背包中可回收数量: " + std::to_string(totalPlayerCount));
    fm.appendLabel("回收单价: §6" + std::to_string(unitPrice) + "§r");


    // 显示耐久度
    if (item.isDamageableItem()) {
        int maxDamage     = item.getItem()->getMaxDamage();
        int currentDamage = item.getDamageValue();
        fm.appendLabel("§a耐久: " + std::to_string(maxDamage - currentDamage) + " / " + std::to_string(maxDamage));
    }

    // 显示特殊值
    short auxValue = item.getAuxValue();
    if (auxValue != 0) {
        fm.appendLabel("§e特殊值: " + std::to_string(auxValue));
    }

    // 获取并显示附魔信息
    if (item.isEnchanted()) {
        ItemEnchants enchants    = item.constructItemEnchantsFromUserData();
        auto         enchantList = enchants.getAllEnchants();
        if (!enchantList.empty()) {
            std::string enchantText = "§d附魔: ";
            for (const auto& enchant : enchantList) {
                enchantText += enchantToString(enchant.mEnchantType) + " " + std::to_string(enchant.mLevel) + " ";
            }
            fm.appendLabel(enchantText);
        }
    }

    fm.appendInput("recycle_count", "请输入回收数量", "1", std::to_string(totalPlayerCount));
    fm.appendLabel("预估回收总价: §6计算中...§r"); // 实际价格在回调中计算

    fm.sendTo(
        player,
        [&player, item, pos, dimId, &region, actualSlotIndex, unitPrice, commissionNbtStr, totalPlayerCount](
            Player&                           p,
            const ll::form::CustomFormResult& result,
            ll::form::FormCancelReason        reason
        ) {
            if (!result.has_value()) {
                p.sendMessage("§c你取消了回收。");
                showRecycleForm(p, pos, dimId, region); // 返回回收商店主界面
                return;
            }

            int recycleCount = 1;
            try {
                recycleCount = std::stoi(std::get<std::string>(result.value().at("recycle_count")));

                if (recycleCount <= 0 || recycleCount > totalPlayerCount) {
                    p.sendMessage(
                        "§c回收数量无效！请输入一个介于1和" + std::to_string(totalPlayerCount) + "之间的整数。"
                    );
                    showRecycleConfirmForm(
                        p,
                        item,
                        pos,
                        dimId,
                        region,
                        actualSlotIndex,
                        unitPrice,
                        commissionNbtStr
                    ); // 重新显示确认表单
                    return;
                }
            } catch (const std::exception& e) {
                p.sendMessage("§c输入无效，请输入正整数。");
                showRecycleConfirmForm(
                    p,
                    item,
                    pos,
                    dimId,
                    region,
                    actualSlotIndex,
                    unitPrice,
                    commissionNbtStr
                ); // 重新显示确认表单
                return;
            }

            // 1. 计算回收价格
            long long recyclePrice = (long long)unitPrice * recycleCount;

            // 跳转到最终确认表单
            showRecycleFinalConfirmForm(
                p,
                item,
                pos,
                dimId,
                region,
                recycleCount,
                recyclePrice,
                commissionNbtStr,
                unitPrice
            );
        }
    );
}

// 简化 getRecyclePrice 函数，只根据单价和数量计算总价
long long getRecyclePrice(int unitPrice, int count) { return (long long)unitPrice * count; }

void showRecycleFinalConfirmForm(
    Player&            player,
    const ItemStack&   item,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    int                recycleCount,
    long long          recyclePrice,
    const std::string& commissionNbtStr,
    int                unitPrice
) {
    ll::form::SimpleForm fm;
    fm.setTitle("确认回收");
    fm.setContent(
        "你确定要回收 " + std::string(item.getName()) + " x" + std::to_string(recycleCount)
        + " 吗？\n"
          "你将获得 §6"
        + std::to_string(recyclePrice)
        + "§r 金币。\n"
          "回收后，你的背包将会刷新。"
    );

    fm.appendButton(
        "§a确认回收",
        [&player, item, pos, dimId, &region, recycleCount, recyclePrice, commissionNbtStr, unitPrice](Player& p) {
            // 2. 获取箱子所有者
            auto [isLocked, ownerUuid, chestType] = getChestDetails(pos, dimId, region);
            if (!isLocked) {
                p.sendMessage("§c回收失败，此箱子不再是回收商店。");
                return;
            }

            // 3. 扣除箱主金钱
            auto ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(ownerUuid));
            if (!ownerInfo) {
                p.sendMessage("§c回收失败，找不到商店主人。");
                return;
            }
            // 检查商店主人余额
            if (LLMoney_Get(ownerInfo->xuid) < recyclePrice) {
                p.sendMessage("§c回收失败，商店主人余额不足。");
                return;
            }

            // 扣除商店主人金钱
            LLMoney_Reduce(ownerInfo->xuid, recyclePrice);

            // 给予玩家金钱
            Economy::addMoney(p, recyclePrice);

            // 4. 从玩家背包移除物品并放入箱子
            auto* blockActor = region.getBlockEntity(pos);
            if (!blockActor) {
                p.sendMessage("§c回收失败，无法获取箱子实体。");
                return;
            }
            auto chest = static_cast<class ChestBlockActor*>(blockActor);
            if (!chest) {
                p.sendMessage("§c回收失败，无法获取箱子实体。");
                return;
            }

            // 预检查箱子容量
            int chestAvailableSpace = 0; // 可以容纳的物品总数
            // 遍历箱子，计算可以堆叠的物品数量和空槽位数量
            for (int i = 0; i < chest->getContainerSize(); ++i) {
                const auto& chestItemInSlot = chest->getItem(i);
                if (chestItemInSlot.isNull()) {
                    // 空槽位，可以放入一个完整堆叠
                    chestAvailableSpace += item.getMaxStackSize();
                } else {
                    // 如果物品类型匹配且未满堆叠，可以堆叠
                    auto chestItemNbt = CT::NbtUtils::getItemNbt(chestItemInSlot);
                    if (chestItemNbt) {
                        auto chestItemNbtForComparison = chestItemNbt->clone();
                        if (chestItemNbtForComparison->contains("Count")) {
                            chestItemNbtForComparison->erase("Count");
                        }
                        std::string currentItemNbtStr = CT::NbtUtils::toSNBT(*chestItemNbtForComparison);

                        if (currentItemNbtStr == commissionNbtStr) {
                            chestAvailableSpace += (item.getMaxStackSize() - chestItemInSlot.mCount);
                        }
                    }
                }
            }

            if (chestAvailableSpace < recycleCount) {
                p.sendMessage("§c回收失败，箱子空间不足，请清理箱子后再试。");
                return;
            }

            // 重新获取委托条件，包括最大回收数量和当前已回收数量
            auto& db         = Sqlite3Wrapper::getInstance();
            auto  commission = db.query(
                "SELECT min_durability, required_enchants, max_recycle_count, "
                "current_recycled_count FROM recycle_shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z "
                "= ? AND item_nbt = ?",
                dimId,
                pos.x,
                pos.y,
                pos.z,
                commissionNbtStr
            );
            if (commission.empty()) {
                p.sendMessage("§c回收失败，无法找到回收委托。");
                return;
            }
            int minDurability = std::stoi(commission[0][0]);
            std::string requiredEnchantsStr = commission[0][1];
            int maxRecycleCount = std::stoi(commission[0][2]);
            int currentRecycledCount = std::stoi(commission[0][3]);

             nlohmann::json requiredEnchants;
            if (!requiredEnchantsStr.empty()) {
                try {
                    requiredEnchants = nlohmann::json::parse(requiredEnchantsStr);
                } catch (const std::exception& e) {
                     logger.error("Failed to parse required_enchants JSON in showRecycleFinalConfirmForm: {}", e.what());
                }
            }

            // 检查是否达到最大回收数量
            if (maxRecycleCount > 0 && (currentRecycledCount + recycleCount) > maxRecycleCount) {
                p.sendMessage("§c回收失败，该委托已达到最大回收数量 (" + std::to_string(maxRecycleCount) + ")。");
                return;
            }

            // 从玩家背包中移除物品
            int   removedCount    = 0;
            auto& playerInventory = p.getInventory();
            for (int i = 0; i < playerInventory.getContainerSize() && removedCount < recycleCount; ++i) {
                const auto& itemInSlot = playerInventory.getItem(i);
                if (itemInSlot.isNull()) continue;

                auto itemNbt = CT::NbtUtils::getItemNbt(itemInSlot);
                if (!itemNbt) continue;
                auto itemNbtForComparison = itemNbt->clone();
                if (itemNbtForComparison->contains("Count")) {
                    itemNbtForComparison->erase("Count");
                }
                std::string itemNbtStr = CT::NbtUtils::toSNBT(*itemNbtForComparison);

                if (itemNbtStr == commissionNbtStr) {
                    // 检查条件
                    if (itemInSlot.isDamageableItem()) {
                        int maxDamage         = itemInSlot.getItem()->getMaxDamage();
                        int currentDamage     = itemInSlot.getDamageValue();
                        int currentDurability = maxDamage - currentDamage;
                        if (currentDurability < minDurability) continue;
                    }
                    if (!requiredEnchants.empty() && requiredEnchants.is_array()) {
                        bool allEnchantsMatch = true;
                        ItemEnchants itemEnchants = itemInSlot.constructItemEnchantsFromUserData();
                        auto allItemEnchants = itemEnchants.getAllEnchants();
                        for (const auto& reqEnchant : requiredEnchants) {
                            bool currentEnchantFound = false;
                            int reqId = reqEnchant["id"];
                            int reqLevel = reqEnchant["level"];
                            for (const auto& itemEnchant : allItemEnchants) {
                                if ((int)itemEnchant.mEnchantType == reqId && itemEnchant.mLevel >= reqLevel) {
                                    currentEnchantFound = true;
                                    break;
                                }
                            }
                            if (!currentEnchantFound) {
                                allEnchantsMatch = false;
                                break;
                            }
                        }
                        if (!allEnchantsMatch) continue;
                    }

                    int countToRemove = std::min((int)itemInSlot.mCount, recycleCount - removedCount);

                    ItemStack itemToRecycle = itemInSlot;
                    itemToRecycle.setStackSize(countToRemove);
                    if (!chest->addItem(itemToRecycle)) {
                        p.sendMessage("§c回收失败，箱子已满。");
                        // 如果有部分物品已经转移，需要回滚或处理
                        return;
                    }

                    playerInventory.removeItem(i, countToRemove);
                    removedCount += countToRemove;
                }
            }

            if (removedCount < recycleCount) {
                p.sendMessage("§c回收失败，背包中可回收物品不足。");
                // 这里需要处理部分回收的情况，比如回滚箱子操作
                return;
            }


            // 5. 更新当前已回收数量
            db.execute(
                "UPDATE recycle_shop_items SET current_recycled_count = current_recycled_count + ? WHERE dim_id = ? "
                "AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_nbt = ?",
                recycleCount,
                dimId,
                pos.x,
                pos.y,
                pos.z,
                commissionNbtStr
            );

            // 6. 记录回收事件
            db.execute(
                "INSERT INTO recycle_records (dim_id, pos_x, pos_y, pos_z, item_nbt, recycler_uuid, recycle_count, "
                "total_price) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                dimId,
                pos.x,
                pos.y,
                pos.z,
                commissionNbtStr,
                p.getUuid().asString(),
                recycleCount,
                (int)recyclePrice
            );

            p.sendMessage(
                "§a成功回收 " + std::string(item.getName()) + " x" + std::to_string(recycleCount) + "，获得 §6"
                + std::to_string(recyclePrice) + "§a 金币。"
            );
            p.refreshInventory(); // 刷新玩家背包

            showRecycleForm(p, pos, dimId, region); // 返回回收商店主界面
        }
    );

    fm.appendButton(
        "§c取消",
        [&player, item, pos, dimId, &region, actualSlotIndex = -1, unitPrice, commissionNbtStr](Player& p) {
            showRecycleConfirmForm(
                p,
                item,
                pos,
                dimId,
                region,
                actualSlotIndex,
                unitPrice,
                commissionNbtStr
            ); // 返回上一个表单
        }
    );
    fm.sendTo(player);
}

void showAddItemToRecycleShopForm(Player& player, BlockPos pos, int dimId, BlockSource& region);
void showSetRecycleItemPriceForm(Player& player, const ItemStack& item, BlockPos pos, int dimId, BlockSource& region);
void showViewRecycleCommissionsForm(Player& player, BlockPos pos, int dimId, BlockSource& region);
void showCommissionDetailsForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    const std::string& commissionNbtStr
);


void showRecycleShopManageForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    fm.setTitle("回收商店管理");
    fm.setContent("选择一个操作：");

    fm.appendButton("添加回收委托", [&player, pos, dimId, &region](Player& p) {
        showAddItemToRecycleShopForm(p, pos, dimId, region);
    });

    fm.appendButton("查看回收委托", [&player, pos, dimId, &region](Player& p) {
        showViewRecycleCommissionsForm(p, pos, dimId, region);
    });

    fm.appendButton("删除回收委托", [&player, pos, dimId, &region](Player& p) {
        // TODO: 实现删除回收委托的表单
        p.sendMessage("§a功能待实现：删除回收委托");
        showRecycleShopManageForm(p, pos, dimId, region); // 返回管理界面
    });

    fm.appendButton("返回", [&player, pos, dimId, &region](Player& p) {
        // 返回到箱子已锁定界面
        std::string   ownerUuid = p.getUuid().asString();
        bool          isLocked  = true;
        CT::ChestType chestType = CT::ChestType::RecycleShop;
        CT::showChestLockForm(p, pos, dimId, isLocked, ownerUuid, chestType, region);
    });

    fm.sendTo(player);
}

void showCommissionDetailsForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    const std::string& commissionNbtStr
) {
    ll::form::SimpleForm fm;
    fm.setTitle("回收记录详情");

    auto itemNbt = CT::NbtUtils::parseSNBT(commissionNbtStr);
    if (!itemNbt) {
        player.sendMessage("§c无法加载物品信息。");
        return;
    }
    itemNbt->at("Count") = ByteTag(1);
    auto itemPtr         = CT::NbtUtils::createItemFromNbt(*itemNbt);
    if (!itemPtr) {
        player.sendMessage("§c无法加载物品信息。");
        return;
    }
    ItemStack item = *itemPtr;

    auto& db      = Sqlite3Wrapper::getInstance();
    auto  records = db.query(
        "SELECT recycler_uuid, recycle_count, total_price, timestamp FROM recycle_records WHERE dim_id = ? AND pos_x "
        "= ? AND pos_y = ? AND pos_z = ? AND item_nbt = ? ORDER BY timestamp DESC",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        commissionNbtStr
    );

    if (records.empty()) {
        fm.setContent("物品: " + std::string(item.getName()) + "\n\n§7该委托暂无回收记录。");
    } else {
        std::string content = "物品: " + std::string(item.getName()) + "\n\n§a最近的回收记录:\n";
        for (const auto& row : records) {
            std::string recyclerUuid = row[0];
            std::string recycleCount = row[1];
            std::string totalPrice   = row[2];
            std::string timestamp    = row[3];

            std::string recyclerName = recyclerUuid; // 默认显示UUID
            auto playerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(recyclerUuid));
            if (playerInfo) {
                recyclerName = playerInfo->name;
            }

            content += "§f" + timestamp + " - " + recyclerName + " 回收了 " + recycleCount + " 个，花费 " + totalPrice
                     + " 金币\n";
        }
        fm.setContent(content);
    }

    fm.appendButton("返回", [&player, pos, dimId, &region](Player& p) {
        showViewRecycleCommissionsForm(p, pos, dimId, region);
    });

    fm.sendTo(player);
}

void showViewRecycleCommissionsForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    fm.setTitle("查看回收委托");

    auto& db          = Sqlite3Wrapper::getInstance();
    auto  commissions = db.query(
        "SELECT item_nbt, price, max_recycle_count, current_recycled_count FROM recycle_shop_items WHERE dim_id = ? "
        "AND pos_x = ? AND pos_y = ? AND pos_z = ?",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    if (commissions.empty()) {
        fm.setContent("该商店没有设置任何回收委托。");
    } else {
        fm.setContent("点击查看每个委托的详细回收记录：");
        for (const auto& row : commissions) {
            std::string itemNbtStr           = row[0];
            int         price                = std::stoi(row[1]);
            int         maxRecycleCount      = std::stoi(row[2]);
            int         currentRecycledCount = std::stoi(row[3]);

            auto itemNbt = CT::NbtUtils::parseSNBT(itemNbtStr);
            if (!itemNbt) continue;
            itemNbt->at("Count") = ByteTag(1);
            auto itemPtr         = CT::NbtUtils::createItemFromNbt(*itemNbt);
            if (!itemPtr) continue;
            ItemStack item = *itemPtr;

            std::string progress = "§7(无限)";
            if (maxRecycleCount > 0) {
                progress = "§a[" + std::to_string(currentRecycledCount) + " / " + std::to_string(maxRecycleCount)
                         + "]§r";
            }

            std::string buttonText =
                std::string(item.getName()) + " §e" + progress + " §6[单价: " + std::to_string(price) + "]§r";
            fm.appendButton(buttonText, [&player, pos, dimId, &region, itemNbtStr](Player& p) {
                showCommissionDetailsForm(p, pos, dimId, region, itemNbtStr);
            });
        }
    }

    fm.appendButton("返回", [&player, pos, dimId, &region](Player& p) {
        showRecycleShopManageForm(p, pos, dimId, region);
    });
    fm.sendTo(player);
}


void showAddItemToRecycleShopForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    fm.setTitle("添加回收委托 - 选择物品");
    fm.setContent("请选择你想要添加回收委托的物品：");

    auto& inventory = player.getInventory();
    for (int i = 0; i < inventory.getContainerSize(); ++i) {
        const auto& item = inventory.getItem(i);
        if (!item.isNull()) {
            std::string buttonText =
                std::string(item.getName()) + " §7(" + item.getTypeName() + ")§r x" + std::to_string(item.mCount);
            std::string itemName = item.getTypeName();
            if (itemName.rfind("minecraft:", 0) == 0) {
                itemName = itemName.substr(10);
            }
            std::string texturePath = CT::ItemTextureManager::getInstance().getTexture(itemName);

            if (!texturePath.empty()) {
                fm.appendButton(buttonText, texturePath, "path", [&player, pos, dimId, &region, item](Player& p) {
                    showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                });
            } else {
                fm.appendButton(buttonText, [&player, pos, dimId, &region, item](Player& p) {
                    showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                });
            }
        }
    }

    fm.appendButton("返回", [&player, pos, dimId, &region](Player& p) {
        showRecycleShopManageForm(p, pos, dimId, region);
    });

    fm.sendTo(player);
}

void showSetRecycleItemPriceForm(Player& player, const ItemStack& item, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::CustomForm fm;
    fm.setTitle("设置回收委托");
    fm.appendLabel("你正在为物品: " + std::string(item.getName()) + " 设置回收委托。");
    fm.appendInput("price_input", "请输入回收价格", "0");

    if (item.isDamageableItem()) {
        fm.appendInput("min_durability", "最低耐久度 (0为不限制)", "0");
    }

    fm.appendInput("required_enchants", "要求附魔 (格式: ID1:等级1,ID2:等级2...)", "");
    fm.appendLabel("例如: 锋利V,耐久III 则输入 9:5,34:3");
    fm.appendLabel("留空则不要求附魔。");
    fm.appendInput("max_recycle_count", "最大回收数量 (0为不限制)", "0"); // 新增最大回收数量输入框


    fm.sendTo(
        player,
        [&player,
         item,
         pos,
         dimId,
         &region](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason reason) {
            if (!result.has_value()) {
                p.sendMessage("§c你取消了设置回收委托。");
                showAddItemToRecycleShopForm(p, pos, dimId, region);
                return;
            }

            try {
                int price = std::stoi(std::get<std::string>(result.value().at("price_input")));
                if (price < 0) {
                    p.sendMessage("§c价格不能为负数！");
                    showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                    return;
                }

                int minDurability = 0;
                if (item.isDamageableItem()) {
                    minDurability = std::stoi(std::get<std::string>(result.value().at("min_durability")));
                    if (minDurability < 0) {
                        p.sendMessage("§c最低耐久度不能为负数！");
                        showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                        return;
                    }
                }

                std::string requiredEnchantsStr =
                    std::get<std::string>(result.value().at("required_enchants"));
                nlohmann::json enchantsJson = nlohmann::json::array();
                if (!requiredEnchantsStr.empty()) {
                    std::stringstream ss(requiredEnchantsStr);
                    std::string       segment;
                    while (std::getline(ss, segment, ',')) {
                        std::stringstream segment_ss(segment);
                        std::string       id_str, level_str;
                        if (std::getline(segment_ss, id_str, ':') && std::getline(segment_ss, level_str)) {
                            try {
                                int id    = std::stoi(id_str);
                                int level = std::stoi(level_str);
                                nlohmann::json enchant;
                                enchant["id"]    = id;
                                enchant["level"] = level;
                                enchantsJson.push_back(enchant);
                            } catch (const std::exception& e) {
                                p.sendMessage("§c附魔格式无效，请使用 ID:等级,ID:等级 的格式。");
                                showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                                return;
                            }
                        }
                    }
                }
                std::string enchantsJsonStr = enchantsJson.is_null() || enchantsJson.empty() ? "" : enchantsJson.dump();


                int maxRecycleCount = std::stoi(std::get<std::string>(result.value().at("max_recycle_count")));
                if (maxRecycleCount < 0) {
                    p.sendMessage("§c最大回收数量不能为负数！");
                    showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                    return;
                }


                // 获取物品NBT
                auto itemNbt = CT::NbtUtils::getItemNbt(item);
                if (!itemNbt) {
                    p.sendMessage("§c无法获取物品NBT数据。");
                    return;
                }

                // 获取物品NBT，并移除数量和损坏标签，以便进行物品类型聚合和数据库存储
                //复修复：不再删除标签，直接存储完整除BT
                std::string itemNbtStr = CT::NbtUtils::toSNBT(*itemNbt);

                // 插入或更新到数据库
                // 注意: 这需要数据库表 recycle_shop_items 包含 min_durability, required_enchants,
                // max_recycle_count, current_recycled_count 列
                auto&       db  = Sqlite3Wrapper::getInstance();
                std::string sql = "INSERT INTO recycle_shop_items (dim_id, pos_x, pos_y, pos_z, item_nbt, price, "
                                  "min_durability, required_enchants, max_recycle_count, "
                                  "current_recycled_count) "
                                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0) " // current_recycled_count 初始化为 0
                                  "ON CONFLICT(dim_id, pos_x, pos_y, pos_z, item_nbt) DO UPDATE SET price = "
                                  "excluded.price, min_durability = excluded.min_durability, required_enchants = "
                                  "excluded.required_enchants, max_recycle_count = excluded.max_recycle_count;";

                if (db.execute(
                        sql,
                        dimId,
                        pos.x,
                        pos.y,
                        pos.z,
                        itemNbtStr,
                        price,
                        minDurability,
                        enchantsJsonStr,
                        maxRecycleCount
                    )) {
                    p.sendMessage(
                        "§a回收委托设置成功！价格: " + std::to_string(price)
                        + "，最大回收数量: " + std::to_string(maxRecycleCount)
                    );
                    FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, ChestType::RecycleShop); // 更新悬浮字
                } else {
                    p.sendMessage("§c回收委托设置失败！请联系管理员检查数据库表结构。");
                }

            } catch (const std::exception& e) {
                p.sendMessage("§c输入无效，请输入一个整数。");
                showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                return;
            }
            showRecycleShopManageForm(p, pos, dimId, region); // 返回管理界面
        }
    );
}


} // namespace CT
