#include "ShopForm.h"
#include "FormUtils.h"
#include "LockForm.h"
#include "Utils/MoneyFormat.h"
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
#include "mc/world/item/Item.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"


namespace CT {

// using CT::NbtUtils::enchantToString; // 现在使用 FormUtils::getItemDisplayString

void showShopChestItemsForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    fm.setTitle("商店物品");

    logger.debug(
        "showShopChestItemsForm: Player {} is opening shop at pos ({},{},{}) dim {}.",
        player.getRealName(),
        pos.x,
        pos.y,
        pos.z,
        dimId
    );

    auto& db      = Sqlite3Wrapper::getInstance();
    auto  results = db.query(
        "SELECT s.item_id, s.price, s.db_count, d.item_nbt FROM shop_items s "
         "JOIN item_definitions d ON s.item_id = d.item_id "
         "WHERE s.dim_id = ? AND s.pos_x = ? AND s.pos_y = ? AND s.pos_z = ?",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    if (results.empty()) {
        fm.setContent("商店是空的，没有可购买的商品。\n");
        logger.debug("showShopChestItemsForm: Shop at pos ({},{},{}) dim {} is empty.", pos.x, pos.y, pos.z, dimId);
    } else {
        logger.debug(
            "showShopChestItemsForm: Found {} items in database for shop at pos ({},{},{}) dim {}.",
            results.size(),
            pos.x,
            pos.y,
            pos.z,
            dimId
        );
        for (const auto& row : results) {
            int         itemId     = std::stoi(row[0]);
            double      price      = std::stod(row[1]); // 修改为 stod
            int         dbCount    = std::stoi(row[2]); // 从数据库获取可售数量
            std::string itemNbtStr = row[3];

            logger.debug(
                "showShopChestItemsForm: Processing item from DB: ItemID={}, NBT='{}', Price={}, DB_Count={}",
                itemId,
                itemNbtStr,
                price,
                dbCount
            );

            auto itemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbtStr);
            if (!itemPtr) {
                fm.appendButton("§c[数据损坏] 无法加载物品", [](Player& p) {
                    p.sendMessage("§c该物品数据已损坏，无法购买。");
                });
                continue;
            }
            ItemStack item = *itemPtr;
            item.mCount    = 1; // 确保 item 的数量为 1，用于后续的 item.matches 比较
            logger.debug("showShopChestItemsForm: Successfully created ItemStack for item '{}'.", item.getName());

            // 计算箱子中该物品类型的总数量
            int totalCount = CT::FormUtils::countItemsInChest(region, pos, dimId, itemNbtStr);
            logger.debug(
                "showShopChestItemsForm: Calculated totalCount in chest for item '{}': {}.",
                item.getName(),
                totalCount
            );

            // 显示数据库中的可售数量 (dbCount) 和箱子中实际存在的数量 (totalCount)
            std::string buttonText = CT::FormUtils::getItemDisplayString(item) + " §b[库存: " + std::to_string(dbCount)
                                   + "/" + std::to_string(totalCount) + "]§r"
                                   + " §6[价格: " + CT::MoneyFormat::format(price) + "]§r";

            std::string texturePath = CT::FormUtils::getItemTexturePath(item);

            logger.debug(
                "showShopChestItemsForm: Button text for item '{}': '{}'. Texture path: '{}'.",
                item.getName(),
                buttonText,
                texturePath
            );

            if (!texturePath.empty()) {
                fm.appendButton(
                    buttonText,
                    texturePath,
                    "path",
                    [pos, dimId, item, itemNbtStr, unitPrice = price](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showShopItemBuyForm(p, item, pos, dimId, 0, unitPrice, region, itemNbtStr);
                    }
                );
            } else {
                fm.appendButton(buttonText, [pos, dimId, item, itemNbtStr, unitPrice = price](Player& p) {
                    auto& region = p.getDimensionBlockSource();
                    showShopItemBuyForm(p, item, pos, dimId, 0, unitPrice, region, itemNbtStr);
                });
            }
        }
    }

    fm.appendButton("返回", [pos, dimId](Player& p) {
        auto& region                          = p.getDimensionBlockSource();
        auto [isLocked, ownerUuid, chestType] = getChestDetails(pos, dimId, region);
        showChestLockForm(p, pos, dimId, isLocked, ownerUuid, chestType, region);
    });
    fm.sendTo(player);
}

void showShopItemPriceForm(Player& player, const ItemStack& item, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::CustomForm fm;
    fm.setTitle("设置商品价格");
    fm.appendLabel("你正在为物品: " + std::string(item.getName()) + " 设置价格。");
    fm.appendInput("price_input", "请输入价格", "0.0"); // 默认值改为小数

    fm.sendTo(
        player,
        [item, pos, dimId](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason reason) {
            if (!result.has_value()) {
                p.sendMessage("§c你取消了设置价格。");
                return;
            }

            try {
                double price = std::stod(std::get<std::string>(result.value().at("price_input"))); // 修改为 stod
                if (price < 0.0) { // 修改为 double 比较
                    p.sendMessage("§c价格不能为负数！");
                    return;
                }

                auto& region  = p.getDimensionBlockSource();
                auto  itemNbt = CT::NbtUtils::getItemNbt(item);
                if (!itemNbt) {
                    p.sendMessage("§c无法获取物品NBT数据。");
                    return;
                }
                std::string itemNbtStr = CT::NbtUtils::toSNBT(*CT::NbtUtils::cleanNbtForComparison(*itemNbt));

                int dbCount = CT::FormUtils::countItemsInChest(region, pos, dimId, itemNbtStr);

                logger.debug(
                    "showShopItemPriceForm: Storing item '{}' with NBT: '{}', Price: {}, Initial DB_Count: {}.",
                    item.getName(),
                    itemNbtStr,
                    price,
                    dbCount
                );

                auto& db = Sqlite3Wrapper::getInstance();

                // 获取或创建 item_id
                int itemId = db.getOrCreateItemId(itemNbtStr);
                if (itemId < 0) {
                    p.sendMessage("§c物品价格和数量设置失败！无法创建物品定义。");
                    logger.error(
                        "showShopItemPriceForm: Failed to get or create item_id for item '{}'.",
                        item.getName()
                    );
                    showShopChestManageForm(p, pos, dimId, region);
                    return;
                }

                std::string sql =
                    "INSERT INTO shop_items (dim_id, pos_x, pos_y, pos_z, slot, item_id, price, db_count) VALUES "
                    "(?, ?, ?, ?, ?, ?, ?, ?) "
                    "ON CONFLICT(dim_id, pos_x, pos_y, pos_z, item_id) DO UPDATE SET price = excluded.price, db_count "
                    "= excluded.db_count, slot = excluded.slot;";
                if (db.execute(sql, dimId, pos.x, pos.y, pos.z, 0, itemId, price, dbCount)) { // price 作为 double 传递
                    p.sendMessage(
                        "§a物品价格和数量设置成功！价格: " + CT::MoneyFormat::format(price)
                        + "，数量: " + std::to_string(dbCount)
                    );
                    logger.debug(
                        "showShopItemPriceForm: Item '{}' price and count set successfully. Price: {}, Count: {}.",
                        item.getName(),
                        price,
                        dbCount
                    );
                    FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, ChestType::Shop);
                } else {
                    p.sendMessage("§c物品价格和数量设置失败！");
                    logger.error(
                        "showShopItemPriceForm: Failed to set item '{}' price and count. Price: {}, Count: {}.",
                        item.getName(),
                        price,
                        dbCount
                    );
                }
            } catch (const std::exception& e) {
                p.sendMessage("§c价格输入无效，请输入一个数字。"); // 提示修改
                logger.error("showShopItemPriceForm: 设置物品价格时发生错误: {}", e.what());
            }
            auto& regionRef = p.getDimensionBlockSource();
            showShopChestManageForm(p, pos, dimId, regionRef);
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

    auto itemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbtStr);
    if (!itemPtr) {
        player.sendMessage("§c无法管理该物品，无法从NBT创建物品。");
        showShopChestManageForm(player, pos, dimId, region);
        return;
    }
    ItemStack item = *itemPtr;

    int totalCount = CT::FormUtils::countItemsInChest(region, pos, dimId, itemNbtStr);

    auto& db = Sqlite3Wrapper::getInstance();

    // 获取 item_id
    int itemId = db.getOrCreateItemId(itemNbtStr);
    if (itemId < 0) {
        player.sendMessage("§c无法管理该物品，无法获取物品ID。");
        showShopChestManageForm(player, pos, dimId, region);
        return;
    }

    auto results = db.query(
        "SELECT price, db_count FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id "
        "= ?",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId
    );

    std::string content = "你正在管理物品: " + std::string(item.getName()) + "\n";
    int         dbCount = 0;
    if (!results.empty()) {
        content += "当前价格: §a" + CT::MoneyFormat::format(std::stod(results[0][0])) + "§r\n";
        dbCount  = std::stoi(results[0][1]);
    } else {
        content += "当前状态: §7未定价§r\n";
    }
    content += "数据库库存: §b" + std::to_string(dbCount) + "§r\n";
    content += "箱子实际库存: §e" + std::to_string(totalCount) + "§r\n";
    fm.setContent(content);

    fm.appendButton("设置价格", [item, pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showShopItemPriceForm(p, item, pos, dimId, region);
    });

    fm.appendButton("移除商品", [pos, dimId, itemId](Player& p) {
        auto& db = Sqlite3Wrapper::getInstance();
        if (db.execute(
                "DELETE FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id = ?",
                dimId,
                pos.x,
                pos.y,
                pos.z,
                itemId
            )) {
            p.sendMessage("§a商品已成功移除！");
            FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, ChestType::Shop);
        } else {
            p.sendMessage("§c商品移除失败！");
        }
        auto& region = p.getDimensionBlockSource();
        showShopChestManageForm(p, pos, dimId, region);
    });

    fm.appendButton("返回", [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showShopChestManageForm(p, pos, dimId, region);
    });

    fm.sendTo(player);
}


void showPurchaseRecordsForm(Player& player, BlockPos pos, int dimId, BlockSource& region);

void showSetShopNameForm(Player& player, BlockPos pos, int dimId, BlockSource& region);

void showShopChestManageForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    fm.setTitle("管理商店箱子");

    logger.debug(
        "showShopChestManageForm: Player {} is managing shop at pos ({},{},{}) dim {}.",
        player.getRealName(),
        pos.x,
        pos.y,
        pos.z,
        dimId
    );

    auto* blockActor = region.getBlockEntity(pos);
    if (!blockActor) {
        player.sendMessage("§c无法获取箱子数据。");
        logger.error("showShopChestManageForm: 无法获取箱子实体在 ({}, {}, {}) in dim {}", pos.x, pos.y, pos.z, dimId);
        return;
    }

    auto chest = static_cast<class ChestBlockActor*>(blockActor);
    if (!chest) {
        player.sendMessage("§c无法获取箱子数据。");
        logger.error(
            "showShopChestManageForm: 无法将 BlockActor 转换为 ChestBlockActor 在 ({}, {}, {}) in dim {}",
            pos.x,
            pos.y,
            pos.z,
            dimId
        );
        return;
    }

    bool                                                           isEmpty = true;
    std::map<std::string, std::tuple<ItemStack, int, std::string>> aggregatedItems;

    for (int i = 0; i < chest->getContainerSize(); ++i) {
        const auto& itemInSlot = chest->getItem(i);
        if (!itemInSlot.isNull()) {
            isEmpty = false;
            logger.debug(
                "showShopChestManageForm: Processing item in slot {}: '{}' (count {}).",
                i,
                itemInSlot.getName(),
                itemInSlot.mCount
            );
            auto itemNbt = CT::NbtUtils::getItemNbt(itemInSlot);
            if (!itemNbt) {
                logger.error("showShopChestManageForm: 无法获取槽位 {} 物品的NBT数据。", i);
                continue;
            }

            std::string itemNbtStr = CT::NbtUtils::toSNBT(*CT::NbtUtils::cleanNbtForComparison(*itemNbt));
            logger.debug("showShopChestManageForm: Item in slot {} NBT (for comparison): {}", i, itemNbtStr);

            if (aggregatedItems.count(itemNbtStr)) {
                std::get<1>(aggregatedItems[itemNbtStr]) += itemInSlot.mCount;
                logger.debug(
                    "showShopChestManageForm: Item '{}' already aggregated. Updated total count to {}.",
                    itemInSlot.getName(),
                    std::get<1>(aggregatedItems[itemNbtStr])
                );
            } else {
                auto& db     = Sqlite3Wrapper::getInstance();
                int   itemId = db.getOrCreateItemId(itemNbtStr);

                std::string priceStr = "§7未定价";
                if (itemId > 0) {
                    auto results = db.query(
                        "SELECT price FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND "
                        "item_id = ?",
                        dimId,
                        pos.x,
                        pos.y,
                        pos.z,
                        itemId
                    );
                    if (!results.empty()) {
                        priceStr = "§a[已定价: " + results[0][0] + "]";
                        logger.debug(
                            "showShopChestManageForm: Item '{}' found in DB with price {}.",
                            itemInSlot.getName(),
                            results[0][0]
                        );
                    } else {
                        logger.debug(
                            "showShopChestManageForm: Item '{}' not found in DB (no price set).",
                            itemInSlot.getName()
                        );
                    }
                }
                aggregatedItems[itemNbtStr] = std::make_tuple(itemInSlot, (int)itemInSlot.mCount, priceStr);
            }
        } else {
            logger.debug("showShopChestManageForm: Slot {} is empty.", i);
        }
    }
    logger.debug(
        "showShopChestManageForm: Finished aggregating items. Found {} unique item types.",
        aggregatedItems.size()
    );

    for (const auto& entry : aggregatedItems) {
        const std::string& itemNbtStr = entry.first;
        const ItemStack&   item       = std::get<0>(entry.second);
        int                totalCount = std::get<1>(entry.second);
        const std::string& priceStr   = std::get<2>(entry.second);

        std::string buttonText  = CT::FormUtils::getItemDisplayString(item, totalCount, true) + " " + priceStr;
        std::string texturePath = CT::FormUtils::getItemTexturePath(item);

        logger.debug(
            "showShopChestManageForm: Adding button for item '{}'. Button text: '{}'. Texture path: '{}'.",
            item.getName(),
            buttonText,
            texturePath
        );

        if (!texturePath.empty()) {
            fm.appendButton(buttonText, texturePath, "path", [pos, dimId, itemNbtStr](Player& p) {
                auto& region = p.getDimensionBlockSource();
                showShopItemManageForm(p, itemNbtStr, pos, dimId, region);
            });
        } else {
            fm.appendButton(buttonText, [pos, dimId, itemNbtStr](Player& p) {
                auto& region = p.getDimensionBlockSource();
                showShopItemManageForm(p, itemNbtStr, pos, dimId, region);
            });
        }
    }

    if (isEmpty) {
        fm.setContent("箱子是空的。\n");
        logger.debug("showShopChestManageForm: Chest at pos ({},{},{}) dim {} is empty.", pos.x, pos.y, pos.z, dimId);
    }

    fm.appendButton("查看购买记录", [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showPurchaseRecordsForm(p, pos, dimId, region);
    });

    fm.appendButton("设置商店名称", [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showSetShopNameForm(p, pos, dimId, region);
    });

    fm.appendButton("返回", [pos, dimId](Player& p) {
        auto& region                          = p.getDimensionBlockSource();
        auto [isLocked, ownerUuid, chestType] = getChestDetails(pos, dimId, region);
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
    double             unitPrice, // 修改为 double
    BlockSource&       region,
    const std::string& itemNbtStr // 添加 itemNbtStr 参数
) {
    ll::form::CustomForm fm;
    fm.setTitle("购买商品");

    logger.debug(
        "showShopItemBuyForm: Player {} is viewing item {} at pos ({},{},{}) dim {} with unitPrice {} and itemNbtStr "
        "{}",
        player.getRealName(),
        item.getName(),
        pos.x,
        pos.y,
        pos.z,
        dimId,
        unitPrice,
        itemNbtStr
    );

    fm.appendLabel(
        "你正在购买物品: " + CT::FormUtils::getItemDisplayString(item, 0, true)
    ); // 使用 FormUtils 显示物品信息

    auto& db     = Sqlite3Wrapper::getInstance();
    int   itemId = db.getOrCreateItemId(itemNbtStr);
    if (itemId > 0) {
        auto results = db.query(
            "SELECT db_count FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND item_id = "
            "?",
            dimId,
            pos.x,
            pos.y,
            pos.z,
            itemId
        );
        if (!results.empty()) {
            fm.appendLabel("剩余库存: §b" + results[0][0] + "§r");
        }
    }

    fm.appendLabel("单价: §6" + CT::MoneyFormat::format(unitPrice) + "§r");
    fm.appendInput("buy_count", "请输入购买数量", "1");
    fm.appendLabel("你的余额: §e" + CT::MoneyFormat::format(Economy::getMoney(player)) + "§r");

    fm.sendTo(
        player,
        [item, pos, dimId, slot, unitPrice, itemNbtStr](
            Player&                           p,
            const ll::form::CustomFormResult& result,
            ll::form::FormCancelReason        reason
        ) {
            auto& region = p.getDimensionBlockSource();
            if (!result.has_value()) {
                logger.debug("showShopItemBuyForm: Player {} cancelled purchase.", p.getRealName());
                p.sendMessage("§c你取消了购买。");
                showShopChestItemsForm(p, pos, dimId, region);
                return;
            }

            int buyCount = 1;
            try {
                buyCount = std::stoi(std::get<std::string>(result.value().at("buy_count")));
                logger.debug("showShopItemBuyForm: Player {} entered buyCount {}.", p.getRealName(), buyCount);
                if (buyCount <= 0) {
                    p.sendMessage("§c购买数量必须大于0！");
                    logger
                        .warn("showShopItemBuyForm: Player {} entered invalid buyCount {}.", p.getRealName(), buyCount);
                    showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr);
                    return;
                }
            } catch (const std::exception& e) {
                p.sendMessage("§c购买数量输入无效，请输入一个正整数。");
                logger
                    .error("showShopItemBuyForm: Error parsing buyCount for player {}: {}", p.getRealName(), e.what());
                showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr);
                return;
            }

            double totalPrice = buyCount * unitPrice; // totalPrice 修改为 double
            logger.debug(
                "showShopItemBuyForm: Player {} attempting to buy {} of item {} for total price {}.",
                p.getRealName(),
                buyCount,
                item.getName(),
                totalPrice
            );

            auto& db     = Sqlite3Wrapper::getInstance();
            int   itemId = db.getOrCreateItemId(itemNbtStr);
            if (itemId < 0) {
                p.sendMessage("§c购买失败，无法获取商品ID。");
                logger.error("showShopItemBuyForm: Failed to get or create item_id for item NBT {}.", itemNbtStr);
                showShopChestItemsForm(p, pos, dimId, region);
                return;
            }

            if (!Economy::hasMoney(p, totalPrice)) {
                p.sendMessage("§c你的金币不足！需要 §6" + CT::MoneyFormat::format(totalPrice) + "§c 金币。");
                logger.warn(
                    "showShopItemBuyForm: Player {} has insufficient money. Needed {}, has {}.",
                    p.getRealName(),
                    totalPrice,
                    Economy::getMoney(p)
                );
                showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr);
                return;
            }

            auto dbResults = db.query(
                "SELECT db_count FROM shop_items WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ? AND "
                "item_id = ?",
                dimId,
                pos.x,
                pos.y,
                pos.z,
                itemId
            );

            if (dbResults.empty()) {
                p.sendMessage("§c商店中没有该商品信息！");
                logger.error(
                    "showShopItemBuyForm: Item NBT {} not found in database for pos ({},{},{}) dim {}.",
                    itemNbtStr,
                    pos.x,
                    pos.y,
                    pos.z,
                    dimId
                );
                showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr);
                return;
            }
            int dbAvailableCount = std::stoi(dbResults[0][0]);
            logger.debug(
                "showShopItemBuyForm: Database available count for item {}: {}.",
                item.getName(),
                dbAvailableCount
            );

            if (dbAvailableCount < buyCount) {
                p.sendMessage("§c商店数据库中没有足够的商品！当前库存: " + std::to_string(dbAvailableCount));
                logger.warn(
                    "showShopItemBuyForm: Database has insufficient items. Needed {}, available {}.",
                    buyCount,
                    dbAvailableCount
                );
                showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr);
                return;
            }

            int actualAvailableItems = CT::FormUtils::countItemsInChest(region, pos, dimId, itemNbtStr);
            logger.debug(
                "showShopItemBuyForm: Actual chest available count for item {}: {}.",
                item.getName(),
                actualAvailableItems
            );

            if (actualAvailableItems < buyCount) {
                p.sendMessage("§c箱子中没有足够的商品！实际库存: " + std::to_string(actualAvailableItems));
                logger.warn(
                    "showShopItemBuyForm: Chest has insufficient items. Needed {}, available {}.",
                    buyCount,
                    actualAvailableItems
                );
                showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr);
                return;
            }

            if (Economy::reduceMoney(p, totalPrice)) {
                // 增加给店主加钱的逻辑
                auto [isLocked, ownerUuid, chestType] = getChestDetails(pos, dimId, region);
                if (isLocked && !ownerUuid.empty()) {
                    auto ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(ownerUuid));
                    if (ownerInfo) {
                        if (Economy::addMoneyByUuid(ownerUuid, totalPrice)) {
                            logger.info(
                                "Successfully added {} money to shop owner {} (uuid: {}).",
                                totalPrice,
                                ownerInfo->name,
                                ownerUuid
                            );
                        } else {
                            logger
                                .error("Failed to add money to shop owner {} (uuid: {}).", ownerInfo->name, ownerUuid);
                        }
                    } else {
                        logger.warn(
                            "Could not find player info for shop owner with UUID {}, cannot add money.",
                            ownerUuid
                        );
                    }
                } else {
                    logger.error(
                        "Shop at pos ({},{},{}) dim {} is not locked or has no owner, cannot add money to owner.",
                        pos.x,
                        pos.y,
                        pos.z,
                        dimId
                    );
                }

                logger.debug(
                    "showShopItemBuyForm: Successfully reduced money for player {}. Total price {}.",
                    p.getRealName(),
                    totalPrice
                );
                auto baseItemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbtStr);
                if (!baseItemPtr) {
                    p.sendMessage("§c购买失败，无法从NBT创建物品。");
                    logger.error("showShopItemBuyForm: Failed to create item from NBT for player {}.", p.getRealName());
                    showShopChestItemsForm(p, pos, dimId, region);
                    return;
                }
                ItemStack baseItem = *baseItemPtr;

                int remainingToGive = buyCount;
                int maxStackSize    = baseItem.getMaxStackSize();
                logger.debug("showShopItemBuyForm: Item '{}' max stack size: {}.", baseItem.getName(), maxStackSize);

                auto* blockActor = region.getBlockEntity(pos);
                if (!blockActor) {
                    p.sendMessage("§c购买失败，无法获取箱子数据。");
                    logger.error(
                        "showShopItemBuyForm: Failed to get ChestBlockActor at ({},{},{}) dim {}.",
                        pos.x,
                        pos.y,
                        pos.z,
                        dimId
                    );
                    showShopChestItemsForm(p, pos, dimId, region);
                    return;
                }
                auto chest = static_cast<class ChestBlockActor*>(blockActor);
                if (!chest) {
                    p.sendMessage("§c购买失败，无法获取箱子数据。");
                    logger.error(
                        "showShopItemBuyForm: Failed to cast BlockActor to ChestBlockActor at ({},{},{}) dim {}.",
                        pos.x,
                        pos.y,
                        pos.z,
                        dimId
                    );
                    showShopChestItemsForm(p, pos, dimId, region);
                    return;
                }

                while (remainingToGive > 0) {
                    int       giveCount  = std::min(remainingToGive, maxStackSize);
                    ItemStack itemToGive = baseItem;
                    itemToGive.mCount    = giveCount;

                    if (p.add(itemToGive)) {
                        logger.debug(
                            "showShopItemBuyForm: Player {} received {} of item {}. Remaining to give {}.",
                            p.getRealName(),
                            giveCount,
                            baseItem.getName(),
                            remainingToGive - giveCount
                        );
                    } else {
                        p.drop(itemToGive, true);
                        p.sendMessage("§c物品栏空间不足，部分物品已掉落。");
                        logger.warn(
                            "showShopItemBuyForm: Player {} inventory full, dropped {} of item {}.",
                            p.getRealName(),
                            giveCount,
                            baseItem.getName()
                        );
                    }
                    remainingToGive -= giveCount;
                }
                p.refreshInventory();
                logger.debug(
                    "showShopItemBuyForm: Player {} finished receiving items. Total received {}.",
                    p.getRealName(),
                    buyCount
                );

                int remainingToBuy = buyCount;
                for (int i = 0; i < chest->getContainerSize() && remainingToBuy > 0; ++i) {
                    const auto& chestItemInSlot = chest->getItem(i);
                    if (!chestItemInSlot.isNull()) {
                        auto chestItemNbt = CT::NbtUtils::getItemNbt(chestItemInSlot);
                        if (chestItemNbt) {
                            auto        cleanedChestItemNbt = CT::NbtUtils::cleanNbtForComparison(*chestItemNbt);
                            std::string currentItemNbtStr   = CT::NbtUtils::toSNBT(*cleanedChestItemNbt);
                            if (currentItemNbtStr == itemNbtStr) {
                                int removeCount = std::min(remainingToBuy, (int)chestItemInSlot.mCount);
                                chest->removeItem(i, removeCount);
                                remainingToBuy -= removeCount;
                                logger.debug(
                                    "showShopItemBuyForm: Removed {} of item {} from slot {}. Remaining to buy {}.",
                                    removeCount,
                                    item.getName(),
                                    i,
                                    remainingToBuy
                                );
                            }
                        }
                    }
                }

                db.execute(
                    "UPDATE shop_items SET db_count = db_count - ? WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND "
                    "pos_z = ? AND item_id = ?",
                    buyCount,
                    dimId,
                    pos.x,
                    pos.y,
                    pos.z,
                    itemId
                );
                logger.debug(
                    "showShopItemBuyForm: Updated database db_count for item {}. Reduced by {}.",
                    item.getName(),
                    buyCount
                );

                db.execute(
                    "INSERT INTO purchase_records (dim_id, pos_x, pos_y, pos_z, item_id, buyer_uuid, purchase_count, "
                    "total_price) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                    dimId,
                    pos.x,
                    pos.y,
                    pos.z,
                    itemId,
                    p.getUuid().asString(),
                    buyCount,
                    totalPrice // totalPrice 作为 double 传递
                );

                p.sendMessage(
                    "§a购买成功！你花费了 §6" + CT::MoneyFormat::format(totalPrice) + "§a 金币购买了 "
                    + std::string(item.getName()) + " x" + std::to_string(buyCount) + "。"
                );
                FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, ChestType::Shop);
            } else {
                p.sendMessage("§c购买失败，金币扣除失败。");
                logger.error("showShopItemBuyForm: Failed to reduce money for player {}.", p.getRealName());
            }
            showShopChestItemsForm(p, pos, dimId, region);
        }
    );
}

void showSetShopNameForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::CustomForm fm;
    fm.setTitle("设置商店名称");

    std::string currentName = getShopName(pos, dimId, region);
    fm.appendLabel("当前商店名称: " + (currentName.empty() ? "§7(未设置)" : "§a" + currentName));
    fm.appendInput("shop_name", "请输入商店名称", "", currentName);

    fm.sendTo(player, [pos, dimId](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
        auto& region = p.getDimensionBlockSource();
        if (!result.has_value()) {
            p.sendMessage("§c你取消了设置商店名称。");
            showShopChestManageForm(p, pos, dimId, region);
            return;
        }

        std::string newName = std::get<std::string>(result.value().at("shop_name"));
        if (setShopName(pos, dimId, region, newName)) {
            p.sendMessage("§a商店名称设置成功！");
        } else {
            p.sendMessage("§c商店名称设置失败！");
        }
        showShopChestManageForm(p, pos, dimId, region);
    });
}

void showPurchaseRecordsForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    // 异步查询购买记录
    auto& db = Sqlite3Wrapper::getInstance();

    // 获取玩家UUID用于后续回调
    std::string playerUuid = player.getUuid().asString();

    logger.debug("showPurchaseRecordsForm: 开始异步查询购买记录 pos({},{},{}) dim {}", pos.x, pos.y, pos.z, dimId);

    // 异步查询数据库
    auto future = db.queryAsync(
        "SELECT p.item_id, p.buyer_uuid, p.purchase_count, p.total_price, p.timestamp, d.item_nbt "
        "FROM purchase_records p "
        "JOIN item_definitions d ON p.item_id = d.item_id "
        "WHERE p.dim_id = ? AND p.pos_x = ? AND p.pos_y = ? AND p.pos_z = ? "
        "ORDER BY p.timestamp DESC",
        dimId,
        pos.x,
        pos.y,
        pos.z
    );

    // 在后台线程等待查询完成，然后回调到主线程显示表单
    std::thread([future = std::move(future), playerUuid, pos, dimId]() mutable {
        try {
            // 等待查询完成
            auto records = future.get();

            logger.debug("showPurchaseRecordsForm: 异步查询完成，记录数: {}", records.size());

            // 回调到主线程显示表单
            ll::thread::ServerThreadExecutor::getDefault().execute(
                [records = std::move(records), playerUuid, pos, dimId]() {
                    // 重新获取玩家对象
                    auto* player = ll::service::getLevel()->getPlayer(mce::UUID::fromString(playerUuid));
                    if (!player) {
                        logger.warn("showPurchaseRecordsForm: 玩家 {} 已离线，无法显示表单", playerUuid);
                        return;
                    }

                    auto* region = &player->getDimensionBlockSource();

                    ll::form::SimpleForm fm;
                    fm.setTitle("购买记录");

                    if (records.empty()) {
                        fm.setContent("§7该商店暂无购买记录。");
                    } else {
                        std::string content = "§a最近的购买记录:\n";
                        for (const auto& row : records) {
                            // int itemId             = std::stoi(row[0]);
                            std::string buyerUuid     = row[1];
                            std::string purchaseCount = row[2];
                            std::string totalPrice    = row[3]; // totalPrice 已经是 string，直接使用
                            std::string timestamp     = row[4];
                            std::string itemNbtStr    = row[5];

                            auto        itemPtr  = CT::FormUtils::createItemStackFromNbtString(itemNbtStr);
                            std::string itemName = "未知物品";
                            if (itemPtr) {
                                itemName = itemPtr->getName();
                            }

                            std::string buyerName = buyerUuid;
                            auto        playerInfo =
                                ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(buyerUuid));
                            if (playerInfo) {
                                buyerName = playerInfo->name;
                            }

                            content += "§f" + timestamp + " - " + buyerName + " 购买了 " + itemName + " x"
                                     + purchaseCount + "，花费 " + totalPrice + " 金币\n";
                        }
                        fm.setContent(content);
                    }

                    fm.appendButton("返回", [pos, dimId, region](Player& p) {
                        showShopChestManageForm(p, pos, dimId, *region);
                    });

                    fm.sendTo(*player);
                }
            );
        } catch (const std::exception& e) {
            logger.error("showPurchaseRecordsForm: 异步查询失败: {}", e.what());

            // 错误时也要回调到主线程通知玩家
            ll::thread::ServerThreadExecutor::getDefault().execute([playerUuid, e_msg = std::string(e.what())]() {
                auto* player = ll::service::getLevel()->getPlayer(mce::UUID::fromString(playerUuid));
                if (player) {
                    player->sendMessage("§c查询购买记录失败: " + e_msg);
                }
            });
        }
    }).detach();
}

} // namespace CT
