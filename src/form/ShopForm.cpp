#include "ShopForm.h"
#include "LockForm.h" // 因为需要调用 showChestLockForm
#include "Utils/ItemTextureManager.h"
#include "Utils/NbtUtils.h"
#include "Utils/economy.h"
#include "db/Sqlite3Wrapper.h"
#include "interaction/chestprotect.h" // 引入 chestprotect 以使用 ChestType
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "mc/world/item/Item.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"

namespace CT {

using CT::NbtUtils::enchantToString;

void showShopChestItemsForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    fm.setTitle("商店物品");

    logger.debug("showShopChestItemsForm: Player {} is opening shop at pos ({},{},{}) dim {}.", player.getRealName(), pos.x, pos.y, pos.z, dimId);

    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT item_nbt, price, db_count FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    if (results.empty()) {
        fm.setContent("商店是空的，没有可购买的商品。\n");
        logger.debug("showShopChestItemsForm: Shop at pos ({},{},{}) dim {} is empty.", pos.x, pos.y, pos.z, dimId);
    } else {
        logger.debug("showShopChestItemsForm: Found {} items in database for shop at pos ({},{},{}) dim {}.", results.size(), pos.x, pos.y, pos.z, dimId);
        for (const auto& row : results) {
            std::string itemNbtStr = row[0];
            int         price      = std::stoi(row[1]);
            int         dbCount    = std::stoi(row[2]); // 从数据库获取可售数量

            logger.debug("showShopChestItemsForm: Processing item from DB: NBT='{}', Price={}, DB_Count={}", itemNbtStr, price, dbCount);

            auto itemNbt = CT::NbtUtils::parseSNBT(itemNbtStr);
            if (!itemNbt) {
                logger.error("showShopChestItemsForm: 无法解析物品NBT: {}", itemNbtStr);
                fm.appendButton("§c[数据损坏] 无法加载物品 (NBT解析失败)", [](Player& p) {
                    p.sendMessage("§c该物品数据已损坏，无法购买。");
                });
                continue;
            }
            itemNbt->at("Count") = ByteTag(1); // 从NBT创建物品需要Count标签
            // 创建一个 ItemStack，其数量设置为 1，用于类型比较和显示名称
            auto itemPtr = CT::NbtUtils::createItemFromNbt(*itemNbt);
            if (!itemPtr) {
                logger.error("showShopChestItemsForm: 无法从NBT创建物品。原始NBT: {}", itemNbtStr);
                fm.appendButton("§c[数据损坏] 无法加载物品 (创建失败)", [](Player& p) {
                    p.sendMessage("§c该物品数据已损坏，无法购买。");
                });
                continue;
            }
            ItemStack item = *itemPtr;
            item.mCount    = 1; // 确保 item 的数量为 1，用于后续的 item.matches 比较
            logger.debug("showShopChestItemsForm: Successfully created ItemStack for item '{}'.", item.getName());

            // 计算箱子中该物品类型的总数量
            auto* blockActor = region.getBlockEntity(pos);
            int   totalCount = 0;
            if (!blockActor) {
                logger.warn("showShopChestItemsForm: 无法获取箱子实体在 ({}, {}, {}) in dim {}", pos.x, pos.y, pos.z, dimId);
            } else {
                auto chest = static_cast<class ChestBlockActor*>(blockActor);
                if (!chest) {
                    logger.error("showShopChestItemsForm: 无法将 BlockActor 转换为 ChestBlockActor 在 ({}, {}, {}) in dim {}", pos.x, pos.y, pos.z, dimId);
                } else {
                    for (int i = 0; i < chest->getContainerSize(); ++i) {
                        const auto& chestItemInSlot = chest->getItem(i);
                        if (!chestItemInSlot.isNull()) {
                            auto chestItemNbt = CT::NbtUtils::getItemNbt(chestItemInSlot);
                            if (chestItemNbt) {
                                auto chestItemNbtForComparison = chestItemNbt->clone();
                                if (chestItemNbtForComparison->contains("Count")) {
                                    chestItemNbtForComparison->erase("Count");
                                }
                                std::string currentItemNbtStr = CT::NbtUtils::toSNBT(*chestItemNbtForComparison);
                                // 使用NBT字符串进行比较，确保与数据库逻辑一致
                                if (currentItemNbtStr == itemNbtStr) {
                                    totalCount += chestItemInSlot.mCount;
                                    logger.debug("showShopChestItemsForm: Slot {} contains matching item '{}' with count {}. Current totalCount {}.", i, chestItemInSlot.getName(), chestItemInSlot.mCount, totalCount);
                                } else {
                                    logger.debug("showShopChestItemsForm: Slot {} contains item '{}' (count {}), but its NBT '{}' does not match the shop item NBT '{}'.", i, chestItemInSlot.getName(), chestItemInSlot.mCount, currentItemNbtStr, itemNbtStr);
                                }
                            } else {
                                logger.warn("showShopChestItemsForm: 无法获取槽位 {} 物品的NBT数据。", i);
                            }
                        } else {
                            logger.debug("showShopChestItemsForm: Slot {} is empty.", i);
                        }
                    }
                }
            }
            logger.debug("showShopChestItemsForm: Calculated totalCount in chest for item '{}': {}.", item.getName(), totalCount);

            // 显示数据库中的可售数量 (dbCount) 和箱子中实际存在的数量 (totalCount)
            std::string buttonText = std::string(item.getName()) + " §7(" + item.getTypeName() + ")§r" + " §b[库存: " + std::to_string(dbCount) + "/"
                                   + std::to_string(totalCount) + "]§r" + " §6[价格: " + std::to_string(price) + "]§r";

            std::string itemInfo;
            // 显示耐久度
            if (item.isDamageableItem()) {
                int maxDamage = item.getItem()->getMaxDamage();
                int currentDamage = item.getDamageValue();
                itemInfo += "\n§a耐久: " + std::to_string(maxDamage - currentDamage) + " / " + std::to_string(maxDamage);
            }

            // 显示特殊值
            short auxValue = item.getAuxValue();
            if (auxValue != 0) {
                itemInfo += "\n§e特殊值: " + std::to_string(auxValue);
            }
            
            buttonText += itemInfo;

            // 获取并显示附魔信息
            if (item.isEnchanted()) {
                ItemEnchants enchants = item.constructItemEnchantsFromUserData();
                auto         enchantList = enchants.getAllEnchants();
                if (!enchantList.empty()) {
                    buttonText += "\n§d附魔: ";
                    for (const auto& enchant : enchantList) {
                        buttonText += enchantToString(enchant.mEnchantType) + " " + std::to_string(enchant.mLevel) + " ";
                    }
                    buttonText += "§r";
                }
            }

            std::string itemName = item.getTypeName();
            if (itemName.rfind("minecraft:", 0) == 0) {
                itemName = itemName.substr(10);
            }
            std::string texturePath = ItemTextureManager::getInstance().getTexture(itemName);

            logger.debug("showShopChestItemsForm: Button text for item '{}': '{}'. Texture path: '{}'.", item.getName(), buttonText, texturePath);

            if (!texturePath.empty()) {
                fm.appendButton(
                    buttonText,
                    texturePath,
                    "path",
                    [&player, pos, dimId, &region, item, itemNbtStr, unitPrice = price](Player& p) { // 捕获 itemNbtStr
                        showShopItemBuyForm(p, item, pos, dimId, 0, unitPrice, region, itemNbtStr); // 传递 itemNbtStr
                    }
                );
            } else {
                fm.appendButton(
                    buttonText,
                    [&player, pos, dimId, &region, item, itemNbtStr, unitPrice = price](Player& p) { // 捕获 itemNbtStr
                        showShopItemBuyForm(p, item, pos, dimId, 0, unitPrice, region, itemNbtStr); // 传递 itemNbtStr
                    }
                );
            }
        }
    }

    fm.appendButton("返回", [&player, pos, dimId, &region](Player& p) {
        // 返回到箱子已锁定界面，这里需要获取箱子锁定状态和主人UUID
        // 实际应用中需要从数据库查询这些信息
        std::string ownerUuid = "";              // 非主人，所以UUID为空
        bool        isLocked  = true;            // 假设箱子是锁定的
        ChestType   chestType = ChestType::Shop; // 假设箱子类型是商店
        showChestLockForm(p, pos, dimId, isLocked, ownerUuid, chestType, region);
    });
    fm.sendTo(player);
}

void showShopItemPriceForm(Player& player, const ItemStack& item, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::CustomForm fm;
    fm.setTitle("设置商品价格");
    fm.appendLabel("你正在为物品: " + std::string(item.getName()) + " 设置价格。");
    fm.appendInput("price_input", "请输入价格", "0");

    fm.sendTo(
        player,
        [&player,
         item,
         pos,
         dimId,
         &region](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason reason) {
            if (!result.has_value()) {
                p.sendMessage("§c你取消了设置价格。");
                return;
            }

            try {
                int price = std::stoi(std::get<std::string>(result.value().at("price_input")));
                if (price < 0) {
                    p.sendMessage("§c价格不能为负数！");
                    return;
                }

                // 获取物品NBT
                auto itemNbt = CT::NbtUtils::getItemNbt(item);
                if (!itemNbt) {
                    p.sendMessage("§c无法获取物品NBT数据。");
                    return;
                }

                // 获取物品NBT，并移除数量标签，以便进行物品类型聚合和数据库存储
                auto itemNbtForStorage = itemNbt->clone();
                if (itemNbtForStorage->contains("Count")) {
                    itemNbtForStorage->erase("Count");
                }
                std::string itemNbtStr = CT::NbtUtils::toSNBT(*itemNbtForStorage);

                // 重新计算箱子中该物品的总数，使用与数据库键相同的NBT字符串比较逻辑
                int   totalCountInChest = 0;
                auto* blockActor        = region.getBlockEntity(pos);
                if (blockActor) {
                    auto chest = static_cast<class ChestBlockActor*>(blockActor);
                    if (chest) {
                        for (int i = 0; i < chest->getContainerSize(); ++i) {
                            const auto& chestItem = chest->getItem(i);
                            if (!chestItem.isNull()) {
                                auto chestItemNbt = CT::NbtUtils::getItemNbt(chestItem);
                                if (chestItemNbt) {
                                    auto chestItemNbtForComparison = chestItemNbt->clone();
                                    if (chestItemNbtForComparison->contains("Count")) {
                                        chestItemNbtForComparison->erase("Count");
                                    }
                                    std::string currentItemNbtStr = CT::NbtUtils::toSNBT(*chestItemNbtForComparison);
                                    if (currentItemNbtStr == itemNbtStr) {
                                        totalCountInChest += chestItem.mCount;
                                    }
                                }
                            }
                        }
                    }
                }
                int dbCount = totalCountInChest; // 使用箱子中的实际总数作为初始可售数量

                logger.debug("showShopItemPriceForm: Storing item '{}' with NBT: '{}', Price: {}, Initial DB_Count: {}.", item.getName(), itemNbtStr, price, dbCount);

                // 插入或更新到数据库，使用 item_nbt 作为冲突解决的依据
                auto&       db = Sqlite3Wrapper::getInstance();
                std::string sql =
                    "INSERT INTO shop_items (dim_id, pos_x, pos_y, pos_z, slot, item_nbt, price, db_count) VALUES "
                    "(?, ?, ?, ?, ?, ?, ?, ?) "
                    "ON CONFLICT(dim_id, pos_x, pos_y, pos_z, item_nbt) DO UPDATE SET price = excluded.price, db_count "
                    "= excluded.db_count;";
                // 这里的 slot 设置为 0，因为价格是针对物品类型而不是特定槽位
                if (db.execute(sql, dimId, pos.x, pos.y, pos.z, 0, itemNbtStr, price, dbCount)) {
                    p.sendMessage(
                        "§a物品价格和数量设置成功！价格: " + std::to_string(price)
                        + "，数量: " + std::to_string(dbCount)
                    );
                    logger.info("showShopItemPriceForm: Item '{}' price and count set successfully. Price: {}, Count: {}.", item.getName(), price, dbCount);
                } else {
                    p.sendMessage("§c物品价格和数量设置失败！");
                    logger.error("showShopItemPriceForm: Failed to set item '{}' price and count. Price: {}, Count: {}.", item.getName(), price, dbCount);
                }
            } catch (const std::exception& e) {
                p.sendMessage("§c价格输入无效，请输入一个整数。");
                logger.error("showShopItemPriceForm: 设置物品价格时发生错误: {}", e.what());
            }
            showShopChestManageForm(p, pos, dimId, region); // 返回管理界面
        }
    );
}

void showShopItemManageForm(
    Player&            player,
    const std::string& itemNbtStr,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region
) {
    ll::form::SimpleForm fm;
    fm.setTitle("管理商品");

    auto itemNbt = CT::NbtUtils::parseSNBT(itemNbtStr);
    if (!itemNbt) {
        logger.error("无法解析物品NBT: {}", itemNbtStr);
        player.sendMessage("§c无法管理该物品，NBT数据无效。");
        showShopChestManageForm(player, pos, dimId, region);
        return;
    }
    itemNbt->at("Count") = ByteTag(1); // 从NBT创建物品需要Count标签
    auto item = CT::NbtUtils::createItemFromNbt(*itemNbt);
    if (!item) {
        logger.error("无法从NBT创建物品。");
        player.sendMessage("§c无法管理该物品，无法从NBT创建物品。");
        showShopChestManageForm(player, pos, dimId, region);
        return;
    }

    // 计算箱子中该物品的总数量
    auto* blockActor = region.getBlockEntity(pos);
    int   totalCount = 0;
    if (blockActor) {
        auto chest = static_cast<class ChestBlockActor*>(blockActor);
        if (chest) {
            for (int i = 0; i < chest->getContainerSize(); ++i) {
                const auto& chestItemInSlot = chest->getItem(i);
                if (!chestItemInSlot.isNull()) {
                    auto chestItemNbt = CT::NbtUtils::getItemNbt(chestItemInSlot);
                    if (chestItemNbt) {
                        auto chestItemNbtForComparison = chestItemNbt->clone();
                        if (chestItemNbtForComparison->contains("Count")) {
                            chestItemNbtForComparison->erase("Count");
                        }
                        std::string currentItemNbtStr = CT::NbtUtils::toSNBT(*chestItemNbtForComparison);
                        if (currentItemNbtStr == itemNbtStr) {
                            totalCount += chestItemInSlot.mCount;
                        }
                    }
                }
            }
        }
    }
    // 查询数据库获取价格和 db_count (使用 itemNbtStr 作为唯一标识)
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT price, db_count FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND "
         "item_nbt = ?",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemNbtStr
    );

    std::string content = "你正在管理物品: " + std::string(item->getName()) + "\n";
    int         dbCount = 0;
    if (!results.empty()) {
        content += "当前价格: §a" + results[0][0] + "§r\n";
        dbCount  = std::stoi(results[0][1]);
    } else {
        content += "当前状态: §7未定价§r\n";
    }
    content += "数据库库存: §b" + std::to_string(dbCount) + "§r\n";
    content += "箱子实际库存: §e" + std::to_string(totalCount) + "§r\n";
    fm.setContent(content);

    fm.appendButton("设置价格", [&player, item = *item, pos, dimId, &region](Player& p) {
        showShopItemPriceForm(p, item, pos, dimId, region);
    });

    fm.appendButton("移除商品", [&player, pos, dimId, &region, itemNbtStr](Player& p) {
        auto& db = Sqlite3Wrapper::getInstance();
        if (db.execute(
                "DELETE FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_nbt = ?",
                dimId,
                pos.x,
                pos.y,
                pos.z,
                itemNbtStr
            )) {
            p.sendMessage("§a商品已成功移除！");
        } else {
            p.sendMessage("§c商品移除失败！");
        }
        showShopChestManageForm(p, pos, dimId, region); // 返回管理界面
    });

    fm.appendButton("返回", [&player, pos, dimId, &region](Player& p) {
        showShopChestManageForm(p, pos, dimId, region);
    });

    fm.sendTo(player);
}


void showShopChestManageForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    fm.setTitle("管理商店箱子");

    logger.debug("showShopChestManageForm: Player {} is managing shop at pos ({},{},{}) dim {}.", player.getRealName(), pos.x, pos.y, pos.z, dimId);

    auto* blockActor = region.getBlockEntity(pos);
    if (!blockActor) {
        player.sendMessage("§c无法获取箱子数据。");
        logger.error("showShopChestManageForm: 无法获取箱子实体在 ({}, {}, {}) in dim {}", pos.x, pos.y, pos.z, dimId);
        return;
    }

    auto chest = static_cast<class ChestBlockActor*>(blockActor);
    if (!chest) {
        player.sendMessage("§c无法获取箱子数据。");
        logger.error("showShopChestManageForm: 无法将 BlockActor 转换为 ChestBlockActor 在 ({}, {}, {}) in dim {}", pos.x, pos.y, pos.z, dimId);
        return;
    }

    bool isEmpty = true;
    // 用于存储聚合后的物品信息 (itemNbtStr -> {ItemStack, totalCount, price})
    std::map<std::string, std::tuple<ItemStack, int, std::string>> aggregatedItems;

    for (int i = 0; i < chest->getContainerSize(); ++i) {
        const auto& itemInSlot = chest->getItem(i);
        if (!itemInSlot.isNull()) {
            isEmpty      = false;
            logger.debug("showShopChestManageForm: Processing item in slot {}: '{}' (count {}).", i, itemInSlot.getName(), itemInSlot.mCount);
            auto itemNbt = CT::NbtUtils::getItemNbt(itemInSlot);
            if (!itemNbt) {
                logger.error("showShopChestManageForm: 无法获取槽位 {} 物品的NBT数据。", i);
                continue;
            }

            // 创建一个NBT副本并移除数量标签，以便进行物品类型聚合
            auto itemNbtForComparison = itemNbt->clone();
            if (itemNbtForComparison->contains("Count")) {
                itemNbtForComparison->erase("Count");
            }
            std::string itemNbtStr = CT::NbtUtils::toSNBT(*itemNbtForComparison);
            logger.debug("showShopChestManageForm: Item in slot {} NBT (for comparison): {}", i, itemNbtStr);

            if (aggregatedItems.count(itemNbtStr)) {
                // 物品已存在，更新数量
                std::get<1>(aggregatedItems[itemNbtStr]) += itemInSlot.mCount;
                logger.debug("showShopChestManageForm: Item '{}' already aggregated. Updated total count to {}.", itemInSlot.getName(), std::get<1>(aggregatedItems[itemNbtStr]));
            } else {
                // 新物品，查询价格并添加
                auto& db      = Sqlite3Wrapper::getInstance();
                auto  results = db.query(
                    "SELECT price FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND "
                     "item_nbt = ?",
                    dimId,
                    pos.x,
                    pos.y,
                    pos.z,
                    itemNbtStr
                );

                std::string priceStr = "§7未定价";
                if (!results.empty()) {
                    priceStr = "§a[已定价: " + results[0][0] + "]";
                    logger.debug("showShopChestManageForm: Item '{}' found in DB with price {}.", itemInSlot.getName(), results[0][0]);
                } else {
                    logger.debug("showShopChestManageForm: Item '{}' not found in DB (no price set).", itemInSlot.getName());
                }
                // 存储原始的ItemStack，但聚合时忽略数量
                aggregatedItems[itemNbtStr] = std::make_tuple(itemInSlot, (int)itemInSlot.mCount, priceStr);
            }
        } else {
            logger.debug("showShopChestManageForm: Slot {} is empty.", i);
        }
    }
    logger.debug("showShopChestManageForm: Finished aggregating items. Found {} unique item types.", aggregatedItems.size());

    for (const auto& entry : aggregatedItems) {
        const std::string& itemNbtStr = entry.first;
        const ItemStack&   item       = std::get<0>(entry.second);
        int                totalCount = std::get<1>(entry.second);
        const std::string& priceStr   = std::get<2>(entry.second);

        std::string buttonText = std::string(item.getName()) + " §7(" + item.getTypeName() + ")§r" + " x" + std::to_string(totalCount) + " " + priceStr;
        std::string itemName   = item.getTypeName();
        if (itemName.rfind("minecraft:", 0) == 0) {
            itemName = itemName.substr(10);
        }
        std::string texturePath = ItemTextureManager::getInstance().getTexture(itemName);

        logger.debug("showShopChestManageForm: Adding button for item '{}'. Button text: '{}'. Texture path: '{}'.", item.getName(), buttonText, texturePath);

        if (!texturePath.empty()) {
            fm.appendButton(buttonText, texturePath, "path", [&player, pos, dimId, &region, itemNbtStr](Player& p) {
                showShopItemManageForm(p, itemNbtStr, pos, dimId, region);
            });
        } else {
            fm.appendButton(buttonText, [&player, pos, dimId, &region, itemNbtStr](Player& p) {
                showShopItemManageForm(p, itemNbtStr, pos, dimId, region);
            });
        }
    }

    if (isEmpty) {
        fm.setContent("箱子是空的。\n");
        logger.debug("showShopChestManageForm: Chest at pos ({},{},{}) dim {} is empty.", pos.x, pos.y, pos.z, dimId);
    }

    fm.appendButton("返回", [&player, pos, dimId, &region](Player& p) {
        // 返回到箱子管理界面，这里需要根据实际情况调整
        // 暂时返回到 showChestLockForm，需要获取箱子锁定状态和主人UUID
        // 为了简化，这里假设箱子是锁定的，并且主人是当前玩家
        // 实际应用中需要从数据库查询这些信息
        std::string ownerUuid = player.getUuid().asString(); // 假设当前玩家是主人
        bool        isLocked  = true;                        // 假设箱子是锁定的
        ChestType   chestType = ChestType::Shop;             // 假设箱子类型是商店
        showChestLockForm(p, pos, dimId, isLocked, ownerUuid, chestType, region);
    });
    fm.sendTo(player);
}

void showShopItemBuyForm(
    Player&            player,
    const ItemStack&   item,
    BlockPos           pos,
    int                dimId,
    int                slot,
    int                unitPrice, // 修改为单价
    BlockSource&       region,
    const std::string& itemNbtStr // 添加 itemNbtStr 参数
) {
    ll::form::CustomForm fm; // 改为 CustomForm
    fm.setTitle("购买商品");

    logger.debug(
        "showShopItemBuyForm: Player {} is viewing item {} at pos ({},{},{}) dim {} with unitPrice {} and itemNbtStr {}",
        player.getRealName(),
        item.getName(),
        pos.x,
        pos.y,
        pos.z,
        dimId,
        unitPrice,
        itemNbtStr
    );

    fm.appendLabel("你正在购买物品: " + std::string(item.getName()) + " §7(" + item.getTypeName() + ")§r" + " (每件)");

    // 查询数据库以获取最新库存
    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT db_count FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_nbt = ?",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemNbtStr
    );
    if (!results.empty()) {
        fm.appendLabel("剩余库存: §b" + results[0][0] + "§r");
    }

    // 显示耐久度
    if (item.isDamageableItem()) {
        int maxDamage = item.getItem()->getMaxDamage();
        int currentDamage = item.getDamageValue();
        fm.appendLabel("§a耐久: " + std::to_string(maxDamage - currentDamage) + " / " + std::to_string(maxDamage));
    }

    // 显示特殊值
    short auxValue = item.getAuxValue();
    if (auxValue != 0) {
        fm.appendLabel("§e特殊值: " + std::to_string(auxValue));
    }

    // 显示附魔信息
    if (item.isEnchanted()) {
        ItemEnchants enchants = item.constructItemEnchantsFromUserData();
        auto         enchantList = enchants.getAllEnchants();
        if (!enchantList.empty()) {
            std::string enchantText = "§d附魔: ";
            for (const auto& enchant : enchantList) {
                enchantText += enchantToString(enchant.mEnchantType) + " " + std::to_string(enchant.mLevel) + " ";
            }
            fm.appendLabel(enchantText);
        }
    }

    // 如果是潜影盒，显示其内部物品
    if (item.getTypeName().find("shulker_box") != std::string::npos) {
        auto itemNbt = CT::NbtUtils::parseSNBT(itemNbtStr);
        if (itemNbt) {
            std::string shulkerContent = CT::NbtUtils::getShulkerBoxItems(*itemNbt);
            if (!shulkerContent.empty()) {
                fm.appendLabel("§7内含: " + shulkerContent + "§r");
            }
        }
    }

    // 如果是收纳袋，显示其内部物品
    if (item.getTypeName().find("bundle") != std::string::npos) {
        auto itemNbt = CT::NbtUtils::parseSNBT(itemNbtStr);
        if (itemNbt) {
            std::string bundleContent = CT::NbtUtils::getBundleItems(*itemNbt);
            if (!bundleContent.empty()) {
                fm.appendLabel("§7内含: " + bundleContent + "§r");
            }
        }
    }

    fm.appendLabel("单价: §6" + std::to_string(unitPrice) + "§r");
    fm.appendInput("buy_count", "请输入购买数量", "1"); // 添加数量输入框
    fm.appendLabel("你的余额: §e" + std::to_string(Economy::getMoney(player)) + "§r");

    fm.sendTo(
        player,
        [&player, item, pos, dimId, slot, unitPrice, &region, itemNbtStr]( // 捕获 itemNbtStr
            Player&                           p,
            const ll::form::CustomFormResult& result,
            ll::form::FormCancelReason        reason
        ) {
            if (!result.has_value()) {
                logger.debug("showShopItemBuyForm: Player {} cancelled purchase.", p.getRealName());
                p.sendMessage("§c你取消了购买。");
                showShopChestItemsForm(p, pos, dimId, region); // 返回商店浏览界面
                return;
            }

            int buyCount = 1;
            try {
                buyCount = std::stoi(std::get<std::string>(result.value().at("buy_count")));
                logger.debug("showShopItemBuyForm: Player {} entered buyCount {}.", p.getRealName(), buyCount);
                if (buyCount <= 0) {
                    p.sendMessage("§c购买数量必须大于0！");
                    logger.warn("showShopItemBuyForm: Player {} entered invalid buyCount {}.", p.getRealName(), buyCount);
                    showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr); // 重新显示购买表单
                    return;
                }
            } catch (const std::exception& e) {
                p.sendMessage("§c购买数量输入无效，请输入一个正整数。");
                logger.error("showShopItemBuyForm: Error parsing buyCount for player {}: {}", p.getRealName(), e.what());
                showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr); // 重新显示购买表单
                return;
            }

            long long totalPrice = (long long)buyCount * unitPrice; // 使用 long long 防止溢出
            logger.debug("showShopItemBuyForm: Player {} attempting to buy {} of item {} for total price {}.", p.getRealName(), buyCount, item.getName(), totalPrice);

            // 检查玩家金币
            if (!Economy::hasMoney(p, totalPrice)) {
                p.sendMessage("§c你的金币不足！需要 §6" + std::to_string(totalPrice) + "§c 金币。");
                logger.warn("showShopItemBuyForm: Player {} has insufficient money. Needed {}, has {}.", p.getRealName(), totalPrice, Economy::getMoney(p));
                showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr); // 重新显示购买表单
                return;
            }

            // 从数据库获取可售数量
            auto& db = Sqlite3Wrapper::getInstance();
            auto  dbResults = db.query(
                "SELECT db_count FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_nbt = ?",
                dimId,
                pos.x,
                pos.y,
                pos.z,
                itemNbtStr
            );

            if (dbResults.empty()) {
                p.sendMessage("§c商店中没有该商品信息！");
                logger.error("showShopItemBuyForm: Item NBT {} not found in database for pos ({},{},{}) dim {}.", itemNbtStr, pos.x, pos.y, pos.z, dimId);
                showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr);
                return;
            }
            int dbAvailableCount = std::stoi(dbResults[0][0]);
            logger.debug("showShopItemBuyForm: Database available count for item {}: {}.", item.getName(), dbAvailableCount);

            if (dbAvailableCount < buyCount) {
                p.sendMessage("§c商店数据库中没有足够的商品！当前库存: " + std::to_string(dbAvailableCount));
                logger.warn("showShopItemBuyForm: Database has insufficient items. Needed {}, available {}.", buyCount, dbAvailableCount);
                showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr); // 重新显示购买表单
                return;
            }

            // 检查箱子中是否有足够的物品 (实际库存)
            auto* blockActor = region.getBlockEntity(pos);
            if (!blockActor) {
                p.sendMessage("§c无法获取箱子数据。");
                logger.error("showShopItemBuyForm: Failed to get ChestBlockActor at ({},{},{}) dim {}.", pos.x, pos.y, pos.z, dimId);
                showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr); // 重新显示购买表单
                return;
            }
            auto chest = static_cast<class ChestBlockActor*>(blockActor);
            if (!chest) {
                p.sendMessage("§c无法获取箱子数据。");
                logger.error(
                    "showShopItemBuyForm: Failed to cast BlockActor to ChestBlockActor at ({},{},{}) dim {}.",
                    pos.x,
                    pos.y,
                    pos.z,
                    dimId
                );
                showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr); // 重新显示购买表单
                return;
            }

            int actualAvailableItems = 0;
            for (int i = 0; i < chest->getContainerSize(); ++i) {
                const auto& chestItemInSlot = chest->getItem(i);
                if (!chestItemInSlot.isNull()) {
                    auto chestItemNbt = CT::NbtUtils::getItemNbt(chestItemInSlot);
                    if (chestItemNbt) {
                        auto chestItemNbtForComparison = chestItemNbt->clone();
                        if (chestItemNbtForComparison->contains("Count")) {
                            chestItemNbtForComparison->erase("Count");
                        }
                        std::string currentItemNbtStr = CT::NbtUtils::toSNBT(*chestItemNbtForComparison);
                        if (currentItemNbtStr == itemNbtStr) {
                            actualAvailableItems += chestItemInSlot.mCount;
                        }
                    }
                }
            }
            logger.debug("showShopItemBuyForm: Actual chest available count for item {}: {}.", item.getName(), actualAvailableItems);

            if (actualAvailableItems < buyCount) {
                p.sendMessage("§c箱子中没有足够的商品！实际库存: " + std::to_string(actualAvailableItems));
                logger.warn("showShopItemBuyForm: Chest has insufficient items. Needed {}, available {}.", buyCount, actualAvailableItems);
                showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr); // 重新显示购买表单
                return;
            }

            // 执行购买
            if (Economy::reduceMoney(p, totalPrice)) {
                logger.debug("showShopItemBuyForm: Successfully reduced money for player {}. Total price {}.", p.getRealName(), totalPrice);
                // 从NBT字符串重新创建物品的NBT，确保Count标签为1，用于创建基础物品
                auto baseItemNbt = CT::NbtUtils::parseSNBT(itemNbtStr);
                if (!baseItemNbt) {
                    p.sendMessage("§c购买失败，无法解析物品NBT。");
                    logger.error("showShopItemBuyForm: Failed to parse item NBT for player {}: {}", p.getRealName(), itemNbtStr);
                    showShopChestItemsForm(p, pos, dimId, region);
                    return;
                }
                baseItemNbt->at("Count") = ByteTag(1); // 确保Count为1，用于创建基础物品
                auto baseItemPtr = CT::NbtUtils::createItemFromNbt(*baseItemNbt);
                if (!baseItemPtr) {
                    p.sendMessage("§c购买失败，无法从NBT创建物品。");
                    logger.error("showShopItemBuyForm: Failed to create item from NBT for player {}.", p.getRealName());
                    showShopChestItemsForm(p, pos, dimId, region);
                    return;
                }
                ItemStack baseItem = *baseItemPtr; // 获取基础物品，不带数量

                // 给予玩家物品，处理堆叠问题
                int remainingToGive = buyCount;
                int maxStackSize = baseItem.getMaxStackSize(); // 获取物品的最大堆叠数量
                logger.debug("showShopItemBuyForm: Item '{}' max stack size: {}.", baseItem.getName(), maxStackSize);

                while (remainingToGive > 0) {
                    int giveCount = std::min(remainingToGive, maxStackSize);
                    ItemStack itemToGive = baseItem; // 从基础物品创建副本
                    itemToGive.mCount = giveCount;   // 设置当前给予的数量

                    if (p.add(itemToGive)) {
                        logger.debug("showShopItemBuyForm: Player {} received {} of item {}. Remaining to give {}.", p.getRealName(), giveCount, baseItem.getName(), remainingToGive - giveCount);
                    } else {
                        p.drop(itemToGive, true); // 给予失败，丢弃物品
                        p.sendMessage("§c物品栏空间不足，部分物品已掉落。");
                        logger.warn("showShopItemBuyForm: Player {} inventory full, dropped {} of item {}.", p.getRealName(), giveCount, baseItem.getName());
                    }
                    remainingToGive -= giveCount;
                }
                p.refreshInventory();
                logger.debug("showShopItemBuyForm: Player {} finished receiving items. Total received {}.", p.getRealName(), buyCount);

                // 从箱子中移除物品
                int remainingToBuy = buyCount;
                for (int i = 0; i < chest->getContainerSize() && remainingToBuy > 0; ++i) {
                    const auto& chestItemInSlot = chest->getItem(i);
                    if (!chestItemInSlot.isNull()) {
                        auto chestItemNbt = CT::NbtUtils::getItemNbt(chestItemInSlot);
                        if (chestItemNbt) {
                            auto chestItemNbtForComparison = chestItemNbt->clone();
                            if (chestItemNbtForComparison->contains("Count")) {
                                chestItemNbtForComparison->erase("Count");
                            }
                            std::string currentItemNbtStr = CT::NbtUtils::toSNBT(*chestItemNbtForComparison);
                            if (currentItemNbtStr == itemNbtStr) {
                                int removeCount = std::min(remainingToBuy, (int)chestItemInSlot.mCount);
                                chest->removeItem(i, removeCount);
                                remainingToBuy -= removeCount;
                                logger.debug("showShopItemBuyForm: Removed {} of item {} from slot {}. Remaining to buy {}.", removeCount, item.getName(), i, remainingToBuy);
                            }
                        }
                    }
                }
 

                // 更新数据库中的 db_count
                db.execute(
                    "UPDATE shop_items SET db_count = db_count - ? WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_nbt = ?",
                    buyCount,
                    dimId,
                    pos.x,
                    pos.y,
                    pos.z,
                    itemNbtStr
                );
                logger.debug("showShopItemBuyForm: Updated database db_count for item {}. Reduced by {}.", item.getName(), buyCount);

                p.sendMessage(
                    "§a购买成功！你花费了 §6" + std::to_string(totalPrice) + "§a 金币购买了 "
                    + std::string(item.getName()) + " x" + std::to_string(buyCount) + "。"
                );
            } else {
                p.sendMessage("§c购买失败，金币扣除失败。");
                logger.error("showShopItemBuyForm: Failed to reduce money for player {}.", p.getRealName());
            }
            showShopChestItemsForm(p, pos, dimId, region); // 返回商店浏览界面
        }
    );

}

} // namespace CT
