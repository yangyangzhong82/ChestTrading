#include "ShopForm.h"
#include "FormUtils.h"
#include "LockForm.h"
#include "Utils/MoneyFormat.h"
#include "Utils/NbtUtils.h"
#include "Utils/economy.h"
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
#include "repository/ItemRepository.h"
#include "repository/ShopRepository.h"
#include "service/ChestService.h"
#include "service/ShopService.h"
#include "service/TextService.h"


namespace CT {

void showShopChestItemsForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    fm.setTitle("商店物品");
    auto& txt = TextService::getInstance();

    logger.debug(
        "showShopChestItemsForm: Player {} is opening shop at pos ({},{},{}) dim {}.",
        player.getRealName(),
        pos.x,
        pos.y,
        pos.z,
        dimId
    );

    auto items = ShopService::getInstance().getShopItems(pos, dimId, region);

    if (items.empty()) {
        fm.setContent(txt.getMessage("shop.empty"));
        logger.debug("showShopChestItemsForm: Shop at pos ({},{},{}) dim {} is empty.", pos.x, pos.y, pos.z, dimId);
    } else {
        logger.debug("showShopChestItemsForm: Found {} items for shop.", items.size());
        for (const auto& shopItem : items) {
            std::string itemNbtStr = ItemRepository::getInstance().getItemNbt(shopItem.itemId);
            if (itemNbtStr.empty()) continue;

            auto itemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbtStr);
            if (!itemPtr) {
                fm.appendButton("§c[数据损坏] 无法加载物品", [](Player& p) {
                    p.sendMessage("§c该物品数据已损坏，无法购买。");
                });
                continue;
            }
            ItemStack item = *itemPtr;
            item.mCount    = 1;

            int         totalCount = ShopService::getInstance().countItemsInChest(region, pos, dimId, itemNbtStr);
            std::string buttonText =
                txt.generateShopItemText(item.getName(), shopItem.price, shopItem.dbCount, totalCount);
            std::string texturePath = CT::FormUtils::getItemTexturePath(item);

            if (!texturePath.empty()) {
                fm.appendButton(
                    buttonText,
                    texturePath,
                    "path",
                    [pos, dimId, item, itemNbtStr, unitPrice = shopItem.price](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showShopItemBuyForm(p, item, pos, dimId, 0, unitPrice, region, itemNbtStr);
                    }
                );
            } else {
                fm.appendButton(buttonText, [pos, dimId, item, itemNbtStr, unitPrice = shopItem.price](Player& p) {
                    auto& region = p.getDimensionBlockSource();
                    showShopItemBuyForm(p, item, pos, dimId, 0, unitPrice, region, itemNbtStr);
                });
            }
        }
    }

    fm.appendButton("返回", [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        auto  info   = ChestService::getInstance().getChestInfo(pos, dimId, region);
        showChestLockForm(
            p,
            pos,
            dimId,
            info.has_value(),
            info ? info->ownerUuid : "",
            info ? info->type : ChestType::Invalid,
            region
        );
    });
    fm.sendTo(player);
}

void showShopItemPriceForm(Player& player, const ItemStack& item, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::CustomForm fm;
    fm.setTitle("设置商品价格");
    fm.appendLabel("你正在为物品: " + std::string(item.getName()) + " 设置价格。");
    fm.appendInput("price_input", "请输入价格", "0.0");

    fm.sendTo(
        player,
        [item, pos, dimId](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason reason) {
            auto& txt = TextService::getInstance();
            if (!result.has_value()) {
                p.sendMessage(txt.getMessage("action.cancelled"));
                return;
            }

            try {
                double price = std::stod(std::get<std::string>(result.value().at("price_input")));
                if (price < 0.0) {
                    p.sendMessage(txt.getMessage("input.negative_price"));
                    return;
                }

                auto& region  = p.getDimensionBlockSource();
                auto  itemNbt = CT::NbtUtils::getItemNbt(item);
                if (!itemNbt) {
                    p.sendMessage("§c无法获取物品NBT数据。");
                    return;
                }
                std::string itemNbtStr = CT::NbtUtils::toSNBT(*CT::NbtUtils::cleanNbtForComparison(*itemNbt));
                int         dbCount    = ShopService::getInstance().countItemsInChest(region, pos, dimId, itemNbtStr);

                auto result = ShopService::getInstance().setItemPrice(pos, dimId, itemNbtStr, price, dbCount, region);
                p.sendMessage(result.message);
            } catch (const std::exception& e) {
                p.sendMessage(txt.getMessage("input.invalid_number"));
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
    auto& txt = TextService::getInstance();

    auto itemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbtStr);
    if (!itemPtr) {
        player.sendMessage("§c无法管理该物品，无法从NBT创建物品。");
        showShopChestManageForm(player, pos, dimId, region);
        return;
    }
    ItemStack item = *itemPtr;

    int totalCount = ShopService::getInstance().countItemsInChest(region, pos, dimId, itemNbtStr);
    int itemId     = ItemRepository::getInstance().getOrCreateItemId(itemNbtStr);
    if (itemId < 0) {
        player.sendMessage("§c无法管理该物品，无法获取物品ID。");
        showShopChestManageForm(player, pos, dimId, region);
        return;
    }

    auto itemOpt = ShopRepository::getInstance().findItem(pos, dimId, itemId);

    std::string content = "你正在管理物品: " + std::string(item.getName()) + "\n";
    int         dbCount = 0;
    if (itemOpt) {
        content += "当前价格: §a" + CT::MoneyFormat::format(itemOpt->price) + "§r\n";
        dbCount  = itemOpt->dbCount;
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
        auto& region = p.getDimensionBlockSource();
        auto& txt    = TextService::getInstance();
        if (ShopService::getInstance().removeItem(pos, dimId, itemId, region)) {
            p.sendMessage(txt.getMessage("shop.item_removed"));
        } else {
            p.sendMessage("§c商品移除失败！");
        }
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
    if (!blockActor || blockActor->mType != BlockActorType::Chest) {
        player.sendMessage("§c无法获取箱子数据。");
        logger.error(
            "showShopChestManageForm: 无法获取箱子实体或类型不匹配在 ({}, {}, {}) in dim {}",
            pos.x,
            pos.y,
            pos.z,
            dimId
        );
        return;
    }

    auto* chest = static_cast<ChestBlockActor*>(blockActor);

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
                int         itemId   = ItemRepository::getInstance().getOrCreateItemId(itemNbtStr);
                std::string priceStr = "§7未定价";
                if (itemId > 0) {
                    auto itemOpt = ShopRepository::getInstance().findItem(pos, dimId, itemId);
                    if (itemOpt) {
                        priceStr = "§a[已定价: " + CT::MoneyFormat::format(itemOpt->price) + "]";
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
        auto& region = p.getDimensionBlockSource();
        auto  info   = ChestService::getInstance().getChestInfo(pos, dimId, region);
        showChestLockForm(
            p,
            pos,
            dimId,
            info.has_value(),
            info ? info->ownerUuid : "",
            info ? info->type : ChestType::Invalid,
            region
        );
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

    int itemId = ItemRepository::getInstance().getOrCreateItemId(itemNbtStr);
    if (itemId > 0) {
        auto itemOpt = ShopRepository::getInstance().findItem(pos, dimId, itemId);
        if (itemOpt) {
            fm.appendLabel("剩余库存: §b" + std::to_string(itemOpt->dbCount) + "§r");
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

            int itemId = ItemRepository::getInstance().getOrCreateItemId(itemNbtStr);
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

            // 先检查箱子中实际物品数量
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

            // 准备工作：创建物品和获取箱子
            auto baseItemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbtStr);
            if (!baseItemPtr) {
                p.sendMessage("§c购买失败，无法从NBT创建物品。");
                logger.error("showShopItemBuyForm: Failed to create item from NBT for player {}.", p.getRealName());
                showShopChestItemsForm(p, pos, dimId, region);
                return;
            }
            ItemStack baseItem = *baseItemPtr;

            auto* blockActor = region.getBlockEntity(pos);
            if (!blockActor || blockActor->mType != BlockActorType::Chest) {
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
            auto* chest = static_cast<ChestBlockActor*>(blockActor);

            // 步骤1：先从箱子扣物品，记录实际扣除数量
            int actualRemoved = 0;
            for (int i = 0; i < chest->getContainerSize() && actualRemoved < buyCount; ++i) {
                const auto& chestItemInSlot = chest->getItem(i);
                if (!chestItemInSlot.isNull()) {
                    auto chestItemNbt = CT::NbtUtils::getItemNbt(chestItemInSlot);
                    if (chestItemNbt) {
                        auto        cleanedChestItemNbt = CT::NbtUtils::cleanNbtForComparison(*chestItemNbt);
                        std::string currentItemNbtStr   = CT::NbtUtils::toSNBT(*cleanedChestItemNbt);
                        if (currentItemNbtStr == itemNbtStr) {
                            int removeCount = std::min(buyCount - actualRemoved, (int)chestItemInSlot.mCount);
                            chest->removeItem(i, removeCount);
                            actualRemoved += removeCount;
                            logger.debug(
                                "showShopItemBuyForm: Removed {} from slot {}. Total: {}.",
                                removeCount,
                                i,
                                actualRemoved
                            );
                        }
                    }
                }
            }

            // 检查是否扣够了
            if (actualRemoved < buyCount) {
                // 箱子物品不够，把已扣的物品放回箱子
                if (actualRemoved > 0) {
                    ItemStack returnItem = baseItem;
                    returnItem.mCount    = actualRemoved;
                    chest->addItem(returnItem);
                }
                p.sendMessage("§c购买失败，箱子中物品数量不足！实际可用: " + std::to_string(actualRemoved));
                logger.warn("showShopItemBuyForm: Chest shortage. Needed {}, got {}.", buyCount, actualRemoved);
                showShopChestItemsForm(p, pos, dimId, region);
                return;
            }

            // 步骤2：箱子扣除成功，现在扣钱
            if (!Economy::reduceMoney(p, totalPrice)) {
                // 扣钱失败，把物品放回箱子
                ItemStack returnItem = baseItem;
                returnItem.mCount    = buyCount;
                chest->addItem(returnItem);
                p.sendMessage("§c购买失败，金币扣除失败。");
                logger.error("showShopItemBuyForm: Failed to reduce money for player {}.", p.getRealName());
                showShopChestItemsForm(p, pos, dimId, region);
                return;
            }

            // 步骤3：扣钱成功，更新数据库库存
            ShopRepository::getInstance().decrementDbCount(pos, dimId, itemId, buyCount);

            // 步骤4：给店主加钱
            auto chestInfo = ChestService::getInstance().getChestInfo(pos, dimId, region);
            if (chestInfo && !chestInfo->ownerUuid.empty()) {
                Economy::addMoneyByUuid(chestInfo->ownerUuid, totalPrice);
            }

            // 步骤5：给玩家物品
            int remainingToGive = buyCount;
            int maxStackSize    = baseItem.getMaxStackSize();
            while (remainingToGive > 0) {
                int       giveCount  = std::min(remainingToGive, maxStackSize);
                ItemStack itemToGive = baseItem;
                itemToGive.mCount    = giveCount;
                if (!p.add(itemToGive)) {
                    p.drop(itemToGive, true);
                    p.sendMessage("§c物品栏空间不足，部分物品已掉落。");
                }
                remainingToGive -= giveCount;
            }
            p.refreshInventory();

            // 步骤6：记录购买
            PurchaseRecordData record;
            record.dimId         = dimId;
            record.pos           = pos;
            record.itemId        = itemId;
            record.buyerUuid     = p.getUuid().asString();
            record.purchaseCount = buyCount;
            record.totalPrice    = totalPrice;
            ShopRepository::getInstance().addPurchaseRecord(record);

            p.sendMessage(
                "§a购买成功！你花费了 §6" + CT::MoneyFormat::format(totalPrice) + "§a 金币购买了 "
                + std::string(item.getName()) + " x" + std::to_string(buyCount) + "。"
            );
            FloatingTextManager::getInstance().updateShopFloatingText(pos, dimId, ChestType::Shop);
            showShopChestItemsForm(p, pos, dimId, region);
        }
    );
}

void showSetShopNameForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::CustomForm fm;
    fm.setTitle("设置商店名称");
    auto& chestService = ChestService::getInstance();
    auto& txt          = TextService::getInstance();

    std::string currentName = chestService.getShopName(pos, dimId, region);
    fm.appendLabel("当前商店名称: " + (currentName.empty() ? "§7(未设置)" : "§a" + currentName));
    fm.appendInput("shop_name", "请输入商店名称", "", currentName);

    fm.sendTo(player, [pos, dimId](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
        auto& region = p.getDimensionBlockSource();
        auto& txt    = TextService::getInstance();
        if (!result.has_value()) {
            p.sendMessage(txt.getMessage("action.cancelled"));
            showShopChestManageForm(p, pos, dimId, region);
            return;
        }

        std::string newName = std::get<std::string>(result.value().at("shop_name"));
        if (ChestService::getInstance().setShopName(pos, dimId, region, newName)) {
            p.sendMessage(txt.getMessage("shop.name_set_success"));
        } else {
            p.sendMessage(txt.getMessage("shop.name_set_fail"));
        }
        showShopChestManageForm(p, pos, dimId, region);
    });
}

void showPurchaseRecordsForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    fm.setTitle("购买记录");

    auto records = ShopRepository::getInstance().getPurchaseRecords(pos, dimId);

    if (records.empty()) {
        fm.setContent("§7该商店暂无购买记录。");
    } else {
        std::string content = "§a最近的购买记录:\n";
        for (const auto& record : records) {
            auto        itemPtr  = CT::FormUtils::createItemStackFromNbtString(record.itemNbt);
            std::string itemName = itemPtr ? itemPtr->getName() : "未知物品";

            std::string buyerName = record.buyerUuid;
            auto playerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(record.buyerUuid));
            if (playerInfo) {
                buyerName = playerInfo->name;
            }

            content += "§f" + record.timestamp + " - " + buyerName + " 购买了 " + itemName + " x"
                     + std::to_string(record.purchaseCount) + "，花费 " + CT::MoneyFormat::format(record.totalPrice)
                     + " 金币\n";
        }
        fm.setContent(content);
    }

    fm.appendButton("返回", [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showShopChestManageForm(p, pos, dimId, region);
    });

    fm.sendTo(player);
}

} // namespace CT
