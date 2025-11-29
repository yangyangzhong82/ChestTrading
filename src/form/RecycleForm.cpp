#include "RecycleForm.h"
#include "LLMoney.h"
#include "LockForm.h"
#include "FormUtils.h" 
#include "Utils/NbtUtils.h"
#include "Utils/economy.h"
#include "db/Sqlite3Wrapper.h"
#include "interaction/chestprotect.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h" 
#include "ll/api/service/PlayerInfo.h"
#include "ll/api/thread/ServerThreadExecutor.h" 
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/item/Item.h" 
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h" 
#include "mc/world/level/Level.h" 
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "nlohmann/json.hpp"


namespace CT {


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
        "SELECT d.item_nbt, r.price, r.min_durability, r.required_enchants "
        "FROM recycle_shop_items r JOIN item_definitions d ON r.item_id = d.item_id "
        "WHERE r.dim_id = ? AND r.pos_x = ? AND r.pos_y = ? AND r.pos_z = ?",
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
            std::string commissionNbtStr    = row[0];
            double      price               = std::stod(row[1]); // 从数据库读取时使用 stod
            int         minDurability       = std::stoi(row[2]);
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
                                   + " §6[回收单价: " + std::to_string(price) + "]§r"; // 使用 std::to_string 显示 double

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
                            itemInfo += NbtUtils::enchantToString((Enchant::Type)id) + " " + std::to_string(level) + " ";
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
    double             recyclePrice, // 修改为 double
    const std::string& commissionNbtStr,
    double             unitPrice     // 修改为 double
);

// 简化 getRecyclePrice 函数，只根据单价和数量计算总价
double getRecyclePrice(double unitPrice, int count) { return unitPrice * count; }

void showRecycleConfirmForm(
    Player&            player,
    const ItemStack&   item,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    int                actualSlotIndex, // This will be -1 when called from the commission list
    double             unitPrice,     // 修改为 double
    const std::string& commissionNbtStr
) {
    ll::form::CustomForm fm;
    fm.setTitle("确认回收物品");

    // 查找玩家背包中所有可回收的此种物品
    int   totalPlayerCount = 0;
    auto& db               = Sqlite3Wrapper::getInstance();
    int   itemId           = db.getOrCreateItemId(commissionNbtStr);
    if (itemId < 0) {
        player.sendMessage("§c无法找到此回收委托的物品定义。");
        return;
    }
    auto commission = db.query(
        "SELECT min_durability, required_enchants FROM recycle_shop_items WHERE dim_id = ? "
        "AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id = ?",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId
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
        auto cleanedNbt = CT::NbtUtils::cleanNbtForComparison(*itemNbt);
        std::string itemNbtStr = CT::NbtUtils::toSNBT(*cleanedNbt);
        
        logger.trace("Comparing inventory item: Cleaned NBT: {}", itemNbtStr);
        logger.trace("Comparing with commission: Commission NBT: {}", commissionNbtStr);

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
    fm.appendLabel("回收单价: §6" + std::to_string(unitPrice) + "§r"); // 使用 std::to_string 显示 double


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
                enchantText += NbtUtils::enchantToString(enchant.mEnchantType) + " " + std::to_string(enchant.mLevel) + " ";
            }
            fm.appendLabel(enchantText);
        }
    }

    fm.appendInput("recycle_count", "请输入回收数量", "1", std::to_string(totalPlayerCount));

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
            double recyclePrice = getRecyclePrice(unitPrice, recycleCount); // 调用更新后的函数，使用 double

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

void showRecycleFinalConfirmForm(
    Player&            player,
    const ItemStack&   item,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    int                recycleCount,
    double             recyclePrice,
    const std::string& commissionNbtStr,
    double             unitPrice
) {
    // 查询最大回收数量
    auto& db     = Sqlite3Wrapper::getInstance();
    int   itemId = db.getOrCreateItemId(commissionNbtStr);
    int   maxRecycleCount = 0;
    if (itemId >= 0) {
        auto commission = db.query(
            "SELECT max_recycle_count FROM recycle_shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id = ?",
            dimId, pos.x, pos.y, pos.z, itemId
        );
        if (!commission.empty()) {
            maxRecycleCount = std::stoi(commission[0][0]);
        }
    }

    ll::form::SimpleForm fm;
    fm.setTitle("确认回收");
    
    std::string content = "你确定要回收 " + std::string(item.getName()) + " x" + std::to_string(recycleCount) + " 吗？\n";
    if (maxRecycleCount > 0) {
        content += "§e最高回收数量: " + std::to_string(maxRecycleCount) + "§r\n";
    }
    content += "你将获得 §6" + std::to_string(recyclePrice) + "§r 金币。\n";
    content += "回收后，你的背包将会刷新。";
    fm.setContent(content);

    fm.appendButton(
        "§a确认回收",
        [&player, item, pos, dimId, &region, recycleCount, recyclePrice, commissionNbtStr, unitPrice](Player& p) {
            // 2. 获取箱子所有者
            auto [isLocked, ownerUuid, chestType] = getChestDetails(pos, dimId, region);
            if (!isLocked) {
                p.sendMessage("§c回收失败，此箱子不再是回收商店。");
                return;
            }

            // 3. 检查店主
            auto ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(ownerUuid));
            if (!ownerInfo) {
                p.sendMessage("§c回收失败，找不到商店主人。");
                return;
            }

            // 提前检查店主余额
            if (Economy::getMoney(ownerInfo->xuid) < recyclePrice) { // getMoney 仍然可以使用 xuid，因为它最终会转换为 uuid
                p.sendMessage("§c回收失败，商店主人余额不足。");
                return;
            }

            // 4. 获取箱子实体
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
            for (int i = 0; i < chest->getContainerSize(); ++i) {
                const auto& chestItemInSlot = chest->getItem(i);
                if (chestItemInSlot.isNull()) {
                    chestAvailableSpace += item.getMaxStackSize();
                } else {
                    auto chestItemNbt = CT::NbtUtils::getItemNbt(chestItemInSlot);
                    if (chestItemNbt) {
                        auto cleanedChestNbt = CT::NbtUtils::cleanNbtForComparison(*chestItemNbt);
                        if (CT::NbtUtils::toSNBT(*cleanedChestNbt) == commissionNbtStr) {
                            chestAvailableSpace += (item.getMaxStackSize() - chestItemInSlot.mCount);
                        }
                    }
                }
            }
            if (chestAvailableSpace < recycleCount) {
                p.sendMessage("§c回收失败，箱子空间不足，请清理箱子后再试。");
                return;
            }

            // 重新获取委托条件
            auto& db     = Sqlite3Wrapper::getInstance();
            int   itemId = db.getOrCreateItemId(commissionNbtStr);
            if (itemId < 0) {
                p.sendMessage("§c回收失败，无法获取物品ID。");
                return;
            }
            auto commission = db.query(
                "SELECT min_durability, required_enchants, max_recycle_count, "
                "current_recycled_count FROM recycle_shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z "
                "= ? AND item_id = ?",
                dimId, pos.x, pos.y, pos.z, itemId
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
            
            // --- 物品转移操作 ---
            int   removedCount    = 0;
            auto& playerInventory = p.getInventory();
            std::vector<std::pair<int, int>> itemsToRemove; // slotIndex, count

            // 1. 查找所有符合条件的物品
            for (int i = 0; i < playerInventory.getContainerSize() && removedCount < recycleCount; ++i) {
                 const auto& itemInSlot = playerInventory.getItem(i);
                if (itemInSlot.isNull()) continue;

                auto itemNbt = CT::NbtUtils::getItemNbt(itemInSlot);
                if (!itemNbt) continue;
                auto cleanedNbt = CT::NbtUtils::cleanNbtForComparison(*itemNbt);
                
                if (CT::NbtUtils::toSNBT(*cleanedNbt) == commissionNbtStr) {
                    // 检查耐久度
                    if (itemInSlot.isDamageableItem()) {
                        int maxDamage         = itemInSlot.getItem()->getMaxDamage();
                        int currentDamage     = itemInSlot.getDamageValue();
                        if ((maxDamage - currentDamage) < minDurability) continue;
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

                    int countCanRemove = std::min((int)itemInSlot.mCount, recycleCount - removedCount);
                    itemsToRemove.push_back({i, countCanRemove});
                    removedCount += countCanRemove;
                }
            }

            // 2. 检查找到的物品数量是否足够
            if (removedCount < recycleCount) {
                p.sendMessage("§c回收失败，背包中可回收物品不足或中途消失。");
                return;
            }

            // 3. 执行物品转移
            for (const auto& itemAction : itemsToRemove) {
                const auto& originalItem = playerInventory.getItem(itemAction.first);
                ItemStack itemToRecycle = originalItem;
                itemToRecycle.setStackSize(itemAction.second);

                if (!chest->addItem(itemToRecycle)) {
                    p.sendMessage("§c回收失败，箱子已满。部分物品可能已转移，请联系管理员处理。");
                    logger.error("Recycle failed mid-transfer due to full chest. Player: {}, Item: {}, Count: {}", p.getRealName(), item.getName(), itemAction.second);
                    // 理想情况下需要回滚已经addItem的物品
                    return;
                }
                playerInventory.removeItem(itemAction.first, itemAction.second);
            }
            
            // --- 物品转移成功后，执行金钱交易和数据更新 ---

            // 4. 扣除商店主人金钱
            if (!Economy::reduceMoneyByUuid(ownerUuid, recyclePrice)) {
                p.sendMessage("§c回收失败，扣除商店主人金钱失败。请联系管理员。");
                logger.error("Recycle failed at money reduction. Player: {}, OwnerUUID: {}, Amount: {}", p.getRealName(), ownerUuid, recyclePrice);
                // 致命错误，物品已转移但钱无法扣除，需要回滚物品
                // 暂时只提示
                return;
            }

            // 5. 给予玩家金钱
            Economy::addMoney(p, recyclePrice);

            // 6. 更新数据库
            db.execute(
                "UPDATE recycle_shop_items SET current_recycled_count = current_recycled_count + ? WHERE dim_id = ? "
                "AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id = ?",
                recycleCount, dimId, pos.x, pos.y, pos.z, itemId
            );

            db.execute(
                "INSERT INTO recycle_records (dim_id, pos_x, pos_y, pos_z, item_id, recycler_uuid, recycle_count, "
                "total_price) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                dimId, pos.x, pos.y, pos.z, itemId, p.getUuid().asString(), recycleCount, recyclePrice // total_price 作为 double 传递
            );

            p.sendMessage(
                "§a成功回收 " + std::string(item.getName()) + " x" + std::to_string(recycleCount) + "，获得 §6"
                + std::to_string(recyclePrice) + "§a 金币。"
            );
            p.refreshInventory();
            showRecycleForm(p, pos, dimId, region);
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
            );
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


void showSetRecycleShopNameForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::CustomForm fm;
    fm.setTitle("设置回收商店名称");
    
    std::string currentName = getShopName(pos, dimId, region);
    fm.appendLabel("当前商店名称: " + (currentName.empty() ? "§7(未设置)" : "§a" + currentName));
    fm.appendInput("shop_name", "请输入商店名称", "", currentName);
    
    fm.sendTo(player, [pos, dimId, &region](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
        if (!result.has_value()) {
            p.sendMessage("§c你取消了设置商店名称。");
            showRecycleShopManageForm(p, pos, dimId, region);
            return;
        }
        
        std::string newName = std::get<std::string>(result.value().at("shop_name"));
        if (setShopName(pos, dimId, region, newName)) {
            p.sendMessage("§a商店名称设置成功！");
        } else {
            p.sendMessage("§c商店名称设置失败！");
        }
        showRecycleShopManageForm(p, pos, dimId, region);
    });
}

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

    fm.appendButton("设置商店名称", [&player, pos, dimId, &region](Player& p) {
        showSetRecycleShopNameForm(p, pos, dimId, region);
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

void showEditCommissionForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    const std::string& commissionNbtStr
) {
    // 先解析物品信息
    auto itemNbt = CT::NbtUtils::parseSNBT(commissionNbtStr);
    if (!itemNbt) {
        player.sendMessage("§c无法加载物品信息。");
        showCommissionDetailsForm(player, pos, dimId, region, commissionNbtStr);
        return;
    }
    itemNbt->at("Count") = ByteTag(1);
    auto itemPtr         = CT::NbtUtils::createItemFromNbt(*itemNbt);
    if (!itemPtr) {
        player.sendMessage("§c无法加载物品信息。");
        showCommissionDetailsForm(player, pos, dimId, region, commissionNbtStr);
        return;
    }
    ItemStack item = *itemPtr;
    
    // 获取当前委托信息
    auto& db     = Sqlite3Wrapper::getInstance();
    int   itemId = db.getOrCreateItemId(commissionNbtStr);
    if (itemId < 0) {
        player.sendMessage("§c无法获取物品ID。");
        showCommissionDetailsForm(player, pos, dimId, region, commissionNbtStr);
        return;
    }
    
    auto commission = db.query(
        "SELECT price, max_recycle_count FROM recycle_shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id = ?",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId
    );
    
    if (commission.empty()) {
        player.sendMessage("§c无法找到该委托信息。");
        showCommissionDetailsForm(player, pos, dimId, region, commissionNbtStr);
        return;
    }
    
    double currentPrice = std::stod(commission[0][0]); // 修改为 stod
    int currentMaxRecycleCount = std::stoi(commission[0][1]);
    
    ll::form::CustomForm fm;
    fm.setTitle("编辑回收委托");
    fm.appendLabel("物品: " + std::string(item.getName()) + " §7(" + item.getTypeName() + ")§r");
    fm.appendInput("price_input", "回收单价", std::to_string(currentPrice), std::to_string(currentPrice)); // 使用 std::to_string 显示 double
    fm.appendInput("max_recycle_count", "最大回收数量 (0为不限制)", std::to_string(currentMaxRecycleCount), std::to_string(currentMaxRecycleCount));
    
    fm.sendTo(
        player,
        [pos, dimId, &region, commissionNbtStr, itemId](
            Player&                           p,
            const ll::form::CustomFormResult& result,
            ll::form::FormCancelReason        reason
        ) {
            if (!result.has_value()) {
                p.sendMessage("§c你取消了编辑。");
                showCommissionDetailsForm(p, pos, dimId, region, commissionNbtStr);
                return;
            }
            
            try {
                double newPrice = std::stod(std::get<std::string>(result.value().at("price_input"))); // 修改为 stod
                if (newPrice < 0.0) { // 修改为 double 比较
                    p.sendMessage("§c价格不能为负数！");
                    showEditCommissionForm(p, pos, dimId, region, commissionNbtStr);
                    return;
                }
                
                int newMaxRecycleCount = std::stoi(std::get<std::string>(result.value().at("max_recycle_count")));
                if (newMaxRecycleCount < 0) {
                    p.sendMessage("§c最大回收数量不能为负数！");
                    showEditCommissionForm(p, pos, dimId, region, commissionNbtStr);
                    return;
                }
                
                // 更新数据库
                auto& db = Sqlite3Wrapper::getInstance();
                if (db.execute(
                    "UPDATE recycle_shop_items SET price = ?, max_recycle_count = ? WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id = ?",
                    newPrice, // price 作为 double 传递
                    newMaxRecycleCount,
                    dimId,
                    pos.x,
                    pos.y,
                    pos.z,
                    itemId
                )) {
                    p.sendMessage("§a委托信息更新成功！新价格: " + std::to_string(newPrice) + "，新最大回收数量: " + std::to_string(newMaxRecycleCount)); // 使用 std::to_string 显示 double
                    FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, ChestType::RecycleShop);
                } else {
                    p.sendMessage("§c委托信息更新失败！");
                }
                
            } catch (const std::exception& e) {
                p.sendMessage("§c输入无效，请输入一个数字。"); // 提示修改
                showEditCommissionForm(p, pos, dimId, region, commissionNbtStr);
                return;
            }
            
            showCommissionDetailsForm(p, pos, dimId, region, commissionNbtStr);
        }
    );
}

void showCommissionDetailsForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    const std::string& commissionNbtStr
) {
    // 先解析物品信息（在主线程）
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
    std::string itemName = itemPtr->getName();
    
    // 异步查询回收记录和委托信息
    auto& db = Sqlite3Wrapper::getInstance();
    
    // 获取玩家UUID用于后续回调
    std::string playerUuid = player.getUuid().asString();
    
    int itemId = db.getOrCreateItemId(commissionNbtStr);
    if (itemId < 0) {
        player.sendMessage("§c无法加载回收记录。");
        return;
    }
    
    logger.debug("showCommissionDetailsForm: 开始异步查询回收记录 pos({},{},{}) dim {} itemId {}", pos.x, pos.y, pos.z, dimId, itemId);
    
    // 异步查询回收记录
    auto recordsFuture = db.queryAsync(
        "SELECT recycler_uuid, recycle_count, total_price, timestamp FROM recycle_records WHERE dim_id = ? AND pos_x "
        "= ? AND pos_y = ? AND pos_z = ? AND item_id = ? ORDER BY timestamp DESC",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId
    );
    
    // 异步查询委托信息
    auto commissionFuture = db.queryAsync(
        "SELECT price, max_recycle_count, current_recycled_count FROM recycle_shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id = ?",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId
    );
    
    // 在后台线程等待查询完成，然后回调到主线程显示表单
    std::thread([recordsFuture = std::move(recordsFuture), commissionFuture = std::move(commissionFuture), playerUuid, pos, dimId, itemName, commissionNbtStr]() mutable {
        try {
            // 等待查询完成
            auto records = recordsFuture.get();
            auto commissionInfo = commissionFuture.get();
            
            logger.debug("showCommissionDetailsForm: 异步查询完成，记录数: {}", records.size());
            
            // 回调到主线程显示表单
            ll::thread::ServerThreadExecutor::getDefault().execute([records = std::move(records), commissionInfo = std::move(commissionInfo), playerUuid, pos, dimId, itemName, commissionNbtStr]() {
                // 重新获取玩家对象
                auto* player = ll::service::getLevel()->getPlayer(mce::UUID::fromString(playerUuid));
                if (!player) {
                    logger.warn("showCommissionDetailsForm: 玩家 {} 已离线，无法显示表单", playerUuid);
                    return;
                }
                
                auto* region = &player->getDimensionBlockSource();
                
                ll::form::SimpleForm fm;
                fm.setTitle("回收记录详情");

                std::string content = "物品: " + itemName + "\n\n";
                
                // 显示委托信息
                if (!commissionInfo.empty()) {
                    double price = std::stod(commissionInfo[0][0]); // 修改为 stod
                    int maxRecycleCount = std::stoi(commissionInfo[0][1]);
                    int currentRecycledCount = std::stoi(commissionInfo[0][2]);
                    
                    content += "§6当前回收单价: " + std::to_string(price) + "§r\n"; // 使用 std::to_string 显示 double
                    if (maxRecycleCount > 0) {
                        content += "§e最大回收数量: " + std::to_string(maxRecycleCount) + "§r\n";
                        content += "§a已回收数量: " + std::to_string(currentRecycledCount) + "§r\n\n";
                    } else {
                        content += "§e最大回收数量: 无限制§r\n\n";
                    }
                }
                
                if (records.empty()) {
                    content += "§7该委托暂无回收记录。";
                } else {
                    content += "§a最近的回收记录:\n";
                    for (const auto& row : records) {
                        std::string recyclerUuid = row[0];
                        std::string recycleCount = row[1];
                        std::string totalPrice   = row[2]; 
                        std::string timestamp    = row[3];

                        std::string recyclerName = recyclerUuid;
                        auto playerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(recyclerUuid));
                        if (playerInfo) {
                            recyclerName = playerInfo->name;
                        }

                        content += "§f" + timestamp + " - " + recyclerName + " 回收了 " + recycleCount + " 个，花费 " + totalPrice
                                 + " 金币\n";
                    }
                }
                fm.setContent(content);

                fm.appendButton("§e编辑委托", [pos, dimId, region, commissionNbtStr](Player& p) {
                    showEditCommissionForm(p, pos, dimId, *region, commissionNbtStr);
                });

                fm.appendButton("返回", [pos, dimId, region](Player& p) {
                    showViewRecycleCommissionsForm(p, pos, dimId, *region);
                });

                fm.sendTo(*player);
            });
        } catch (const std::exception& e) {
            logger.error("showCommissionDetailsForm: 异步查询失败: {}", e.what());
            
            // 错误时也要回调到主线程通知玩家
            ll::thread::ServerThreadExecutor::getDefault().execute([playerUuid, e_msg = std::string(e.what())]() {
                auto* player = ll::service::getLevel()->getPlayer(mce::UUID::fromString(playerUuid));
                if (player) {
                    player->sendMessage("§c查询回收记录详情失败: " + e_msg);
                }
            });
        }
    }).detach();
}

void showViewRecycleCommissionsForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    // 异步查询回收委托列表
    auto& db = Sqlite3Wrapper::getInstance();
    
    // 获取玩家UUID用于后续回调
    std::string playerUuid = player.getUuid().asString();
    
    logger.debug("showViewRecycleCommissionsForm: 开始异步查询回收委托 pos({},{},{}) dim {}", pos.x, pos.y, pos.z, dimId);
    
    // 异步查询数据库
    auto future = db.queryAsync(
        "SELECT d.item_nbt, r.price, r.max_recycle_count, r.current_recycled_count "
        "FROM recycle_shop_items r JOIN item_definitions d ON r.item_id = d.item_id "
        "WHERE r.dim_id = ? AND r.pos_x = ? AND r.pos_y = ? AND r.pos_z = ?",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );
    
    // 在后台线程等待查询完成，然后回调到主线程显示表单
    std::thread([future = std::move(future), playerUuid, pos, dimId]() mutable {
        try {
            // 等待查询完成
            auto commissions = future.get();
            
            logger.debug("showViewRecycleCommissionsForm: 异步查询完成，委托数: {}", commissions.size());
            
            // 回调到主线程显示表单
            ll::thread::ServerThreadExecutor::getDefault().execute([commissions = std::move(commissions), playerUuid, pos, dimId]() {
                // 重新获取玩家对象
                auto* player = ll::service::getLevel()->getPlayer(mce::UUID::fromString(playerUuid));
                if (!player) {
                    logger.warn("showViewRecycleCommissionsForm: 玩家 {} 已离线，无法显示表单", playerUuid);
                    return;
                }
                
                auto* region = &player->getDimensionBlockSource();
                
                ll::form::SimpleForm fm;
                fm.setTitle("查看回收委托");

                if (commissions.empty()) {
                    fm.setContent("该商店没有设置任何回收委托。");
                } else {
                    fm.setContent("点击查看每个委托的详细回收记录：");
                    for (const auto& row : commissions) {
                        std::string itemNbtStr           = row[0];
                        double      price                = std::stod(row[1]); // 修改为 stod
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
                            std::string(item.getName()) + " §e" + progress + " §6[单价: " + std::to_string(price) + "]§r"; // 使用 std::to_string 显示 double
                        fm.appendButton(buttonText, [pos, dimId, region, itemNbtStr](Player& p) {
                            showCommissionDetailsForm(p, pos, dimId, *region, itemNbtStr);
                        });
                    }
                }

                fm.appendButton("返回", [pos, dimId, region](Player& p) {
                    showRecycleShopManageForm(p, pos, dimId, *region);
                });
                
                fm.sendTo(*player);
            });
        } catch (const std::exception& e) {
            logger.error("showViewRecycleCommissionsForm: 异步查询失败: {}", e.what());
            
            // 错误时也要回调到主线程通知玩家
            ll::thread::ServerThreadExecutor::getDefault().execute([playerUuid, e_msg = std::string(e.what())]() {
                auto* player = ll::service::getLevel()->getPlayer(mce::UUID::fromString(playerUuid));
                if (player) {
                    player->sendMessage("§c查询回收委托失败: " + e_msg);
                }
            });
        }
    }).detach();
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
    fm.appendInput("price_input", "请输入回收价格", "0.0"); // 更改默认值为 "0.0"

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
                double price = std::stod(std::get<std::string>(result.value().at("price_input"))); // 直接解析为 double
                if (price < 0.0) {
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
                auto cleanedNbt = CT::NbtUtils::cleanNbtForComparison(*itemNbt);
                std::string itemNbtStr = CT::NbtUtils::toSNBT(*cleanedNbt);
                
                logger.info("Setting recycle commission for item '{}'.", item.getName());
                logger.info("Original NBT: {}", CT::NbtUtils::toSNBT(*itemNbt));
                logger.info("Cleaned NBT for DB: {}", itemNbtStr);

                // 插入或更新到数据库
                auto& db     = Sqlite3Wrapper::getInstance();
                int   itemId = db.getOrCreateItemId(itemNbtStr);
                if (itemId < 0) {
                    p.sendMessage("§c回收委托设置失败！无法创建物品定义。");
                    return;
                }

                std::string sql = "INSERT INTO recycle_shop_items (dim_id, pos_x, pos_y, pos_z, item_id, price, "
                                  "min_durability, required_enchants, max_recycle_count, current_recycled_count) "
                                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0) "
                                  "ON CONFLICT(dim_id, pos_x, pos_y, pos_z, item_id) DO UPDATE SET price = "
                                  "excluded.price, min_durability = excluded.min_durability, required_enchants = "
                                  "excluded.required_enchants, max_recycle_count = excluded.max_recycle_count;";

                if (db.execute(
                        sql,
                        dimId,
                        pos.x,
                        pos.y,
                        pos.z,
                        itemId,
                        price, // price 作为 double 传递
                        minDurability,
                        enchantsJsonStr,
                        maxRecycleCount
                    )) {
                    p.sendMessage(
                        "§a回收委托设置成功！价格: " + std::to_string(price) // 使用 std::to_string 显示 double
                        + "，最大回收数量: " + std::to_string(maxRecycleCount)
                    );
                    FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, ChestType::RecycleShop); // 更新悬浮字
                } else {
                    p.sendMessage("§c回收委托设置失败！请联系管理员检查数据库表结构。");
                }

            } catch (const std::exception& e) {
                p.sendMessage("§c输入无效，请输入一个数字。"); // 提示修改为数字
                showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                return;
            }
            showRecycleShopManageForm(p, pos, dimId, region); // 返回管理界面
        }
    );
}


} // namespace CT
