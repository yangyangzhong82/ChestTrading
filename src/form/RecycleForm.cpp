#include "RecycleForm.h"
#include "FormUtils.h"
#include "LLMoney.h"
#include "LockForm.h"
#include "Utils/MoneyFormat.h"
#include "Utils/NbtUtils.h"
#include "Utils/economy.h"
#include "db/Sqlite3Wrapper.h"
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
#include "repository/ItemRepository.h"
#include "repository/ShopRepository.h"
#include "service/ChestService.h"
#include "service/RecycleService.h"
#include "service/TextService.h"


namespace CT {


void showRecycleForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    showRecycleItemListForm(player, pos, dimId, region);
}

void showRecycleItemListForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    fm.setTitle("回收商店 - 物品列表");
    auto& txt = TextService::getInstance();

    auto commissions = RecycleService::getInstance().getCommissions(pos, dimId);

    if (commissions.empty()) {
        fm.setContent(txt.getMessage("recycle.empty"));
    } else {
        fm.setContent(txt.getMessage("recycle.list_content"));
        for (const auto& commission : commissions) {
            auto itemNbt = CT::NbtUtils::parseSNBT(commission.itemNbt);
            if (!itemNbt) {
                fm.appendButton("§c[数据损坏] 无法加载委托物品 (NBT解析失败)", [](Player& p) {
                    p.sendMessage(TextService::getInstance().getMessage("recycle.data_corrupt"));
                });
                continue;
            }
            itemNbt->at("Count") = ByteTag(1);
            auto itemPtr         = CT::NbtUtils::createItemFromNbt(*itemNbt);
            if (!itemPtr) {
                fm.appendButton("§c[数据损坏] 无法加载委托物品 (创建失败)", [](Player& p) {
                    p.sendMessage(TextService::getInstance().getMessage("recycle.data_corrupt"));
                });
                continue;
            }
            ItemStack item = *itemPtr;
            item.mCount    = 1;

            std::string buttonText = std::string(item.getName()) + " §7(" + item.getTypeName() + ")§r"
                                   + " §6[回收单价: " + CT::MoneyFormat::format(commission.price) + "]§r";

            std::string itemInfo;
            if (commission.minDurability > 0) {
                itemInfo += "\n§a最低耐久: " + std::to_string(commission.minDurability) + "§r";
            }
            if (!commission.requiredEnchants.empty()) {
                try {
                    nlohmann::json enchants = nlohmann::json::parse(commission.requiredEnchants);
                    if (enchants.is_array() && !enchants.empty()) {
                        itemInfo += "\n§d要求附魔: ";
                        for (const auto& enchant : enchants) {
                            int id    = enchant["id"];
                            int level = enchant["level"];
                            itemInfo +=
                                NbtUtils::enchantToString((Enchant::Type)id) + " " + std::to_string(level) + " ";
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
            std::string texturePath      = CT::ItemTextureManager::getInstance().getTexture(itemName);
            std::string commissionNbtStr = commission.itemNbt;
            double      price            = commission.price;

            if (!texturePath.empty()) {
                fm.appendButton(
                    buttonText,
                    texturePath,
                    "path",
                    [pos, dimId, item, price, commissionNbtStr](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showRecycleConfirmForm(p, item, pos, dimId, region, -1, price, commissionNbtStr);
                    }
                );
            } else {
                fm.appendButton(buttonText, [pos, dimId, item, price, commissionNbtStr](Player& p) {
                    auto& region = p.getDimensionBlockSource();
                    showRecycleConfirmForm(p, item, pos, dimId, region, -1, price, commissionNbtStr);
                });
            }
        }
    }

    fm.appendButton("返回", [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        auto  info   = ChestService::getInstance().getChestInfo(pos, dimId, region);
        CT::showChestLockForm(
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

void showRecycleFinalConfirmForm(
    Player&            player,
    const ItemStack&   item,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    int                recycleCount,
    double             recyclePrice, // 修改为 double
    const std::string& commissionNbtStr,
    double             unitPrice // 修改为 double
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
    double             unitPrice,       // 修改为 double
    const std::string& commissionNbtStr
) {
    ll::form::CustomForm fm;
    fm.setTitle("确认回收物品");
    auto& txt = TextService::getInstance();

    int totalPlayerCount = 0;
    int itemId           = ItemRepository::getInstance().getOrCreateItemId(commissionNbtStr);
    if (itemId < 0) {
        player.sendMessage(txt.getMessage("recycle.no_item_def"));
        return;
    }

    auto commission = RecycleService::getInstance().getCommission(pos, dimId, itemId);
    if (!commission) {
        player.sendMessage(txt.getMessage("recycle.no_commission"));
        return;
    }

    int            minDurability = commission->minDurability;
    nlohmann::json requiredEnchants;
    if (!commission->requiredEnchants.empty()) {
        try {
            requiredEnchants = nlohmann::json::parse(commission->requiredEnchants);
        } catch (const std::exception& e) {
            logger.error("Failed to parse required_enchants JSON in showRecycleConfirmForm: {}", e.what());
        }
    }


    for (int i = 0; i < player.getInventory().getContainerSize(); ++i) {
        const auto& itemInSlot = player.getInventory().getItem(i);
        if (itemInSlot.isNull()) continue;

        auto itemNbt = CT::NbtUtils::getItemNbt(itemInSlot);
        if (!itemNbt) continue;
        auto        cleanedNbt = CT::NbtUtils::cleanNbtForComparison(*itemNbt);
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
                bool         allEnchantsMatch = true;
                ItemEnchants itemEnchants     = itemInSlot.constructItemEnchantsFromUserData();
                auto         allItemEnchants  = itemEnchants.getAllEnchants();

                for (const auto& reqEnchant : requiredEnchants) {
                    bool currentEnchantFound = false;
                    int  reqId               = reqEnchant["id"];
                    int  reqLevel            = reqEnchant["level"];
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
    fm.appendLabel("回收单价: §6" + CT::MoneyFormat::format(unitPrice) + "§r");


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
                enchantText +=
                    NbtUtils::enchantToString(enchant.mEnchantType) + " " + std::to_string(enchant.mLevel) + " ";
            }
            fm.appendLabel(enchantText);
        }
    }

    fm.appendInput("recycle_count", "请输入回收数量", "1", std::to_string(totalPlayerCount));

    fm.sendTo(
        player,
        [item, pos, dimId, actualSlotIndex, unitPrice, commissionNbtStr, totalPlayerCount](
            Player&                           p,
            const ll::form::CustomFormResult& result,
            ll::form::FormCancelReason        reason
        ) {
            auto& region = p.getDimensionBlockSource();
            auto& txt    = TextService::getInstance();
            if (!result.has_value()) {
                p.sendMessage(txt.getMessage("action.cancelled"));
                showRecycleForm(p, pos, dimId, region); // 返回回收商店主界面
                return;
            }

            int recycleCount = 1;
            try {
                recycleCount = std::stoi(std::get<std::string>(result.value().at("recycle_count")));

                if (recycleCount <= 0 || recycleCount > totalPlayerCount) {
                    p.sendMessage(txt.getMessage(
                        "input.invalid_recycle_count",
                        {
                            {"max", std::to_string(totalPlayerCount)}
                    }
                    ));
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
                p.sendMessage(txt.getMessage("input.invalid_number"));
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
    auto& txt             = TextService::getInstance();
    int   itemId          = ItemRepository::getInstance().getOrCreateItemId(commissionNbtStr);
    int   maxRecycleCount = 0;
    if (itemId >= 0) {
        auto commission = RecycleService::getInstance().getCommission(pos, dimId, itemId);
        if (commission) {
            maxRecycleCount = commission->maxRecycleCount;
        }
    }

    ll::form::SimpleForm fm;
    fm.setTitle("确认回收");

    std::string content =
        "你确定要回收 " + std::string(item.getName()) + " x" + std::to_string(recycleCount) + " 吗？\n";
    if (maxRecycleCount > 0) {
        content += "§e最高回收数量: " + std::to_string(maxRecycleCount) + "§r\n";
    }
    content += "你将获得 §6" + CT::MoneyFormat::format(recyclePrice) + "§r 金币。\n";
    content += "回收后，你的背包将会刷新。";
    fm.setContent(content);

    fm.appendButton(
        "§a确认回收",
        [item, pos, dimId, recycleCount, recyclePrice, commissionNbtStr, unitPrice](Player& p) {
            auto& region = p.getDimensionBlockSource();
            auto& txt    = TextService::getInstance();

            int itemId = ItemRepository::getInstance().getOrCreateItemId(commissionNbtStr);
            if (itemId < 0) {
                p.sendMessage(txt.getMessage("recycle.item_id_fail"));
                return;
            }

            // 调用服务层执行完整回收流程
            auto result =
                RecycleService::getInstance()
                    .executeFullRecycle(p, pos, dimId, itemId, recycleCount, unitPrice, commissionNbtStr, region);

            if (!result.success) {
                p.sendMessage(result.message);
                return;
            }

            p.sendMessage(txt.getMessage(
                "recycle.success",
                {
                    {"item",  std::string(item.getName())          },
                    {"count", std::to_string(recycleCount)         },
                    {"price", CT::MoneyFormat::format(recyclePrice)}
            }
            ));
            showRecycleForm(p, pos, dimId, region);
        }
    );

    fm.appendButton("§c取消", [item, pos, dimId, unitPrice, commissionNbtStr](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showRecycleConfirmForm(p, item, pos, dimId, region, -1, unitPrice, commissionNbtStr);
    });
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
    auto& txt = TextService::getInstance();

    std::string currentName = ChestService::getInstance().getShopName(pos, dimId, region);
    fm.appendLabel("当前商店名称: " + (currentName.empty() ? "§7(未设置)" : "§a" + currentName));
    fm.appendInput("shop_name", "请输入商店名称", "", currentName);

    fm.sendTo(player, [pos, dimId](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
        auto& region = p.getDimensionBlockSource();
        auto& txt    = TextService::getInstance();
        if (!result.has_value()) {
            p.sendMessage(txt.getMessage("action.cancelled"));
            showRecycleShopManageForm(p, pos, dimId, region);
            return;
        }

        std::string newName = std::get<std::string>(result.value().at("shop_name"));
        if (ChestService::getInstance().setShopName(pos, dimId, region, newName)) {
            p.sendMessage(txt.getMessage("shop.name_set_success"));
        } else {
            p.sendMessage(txt.getMessage("shop.name_set_fail"));
        }
        showRecycleShopManageForm(p, pos, dimId, region);
    });
}

void showRecycleShopManageForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    fm.setTitle("回收商店管理");
    fm.setContent("选择一个操作：");

    fm.appendButton("添加回收委托", [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showAddItemToRecycleShopForm(p, pos, dimId, region);
    });

    fm.appendButton("查看回收委托", [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showViewRecycleCommissionsForm(p, pos, dimId, region);
    });

    fm.appendButton("设置商店名称", [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showSetRecycleShopNameForm(p, pos, dimId, region);
    });

    fm.appendButton("返回", [pos, dimId](Player& p) {
        // 返回到箱子已锁定界面
        auto& region = p.getDimensionBlockSource();
        auto  info   = ChestService::getInstance().getChestInfo(pos, dimId, region);
        CT::showChestLockForm(
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

void showEditCommissionForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    const std::string& commissionNbtStr
) {
    auto& txt = TextService::getInstance();

    auto itemNbt = CT::NbtUtils::parseSNBT(commissionNbtStr);
    if (!itemNbt) {
        player.sendMessage(txt.getMessage("recycle.load_fail"));
        showCommissionDetailsForm(player, pos, dimId, region, commissionNbtStr);
        return;
    }
    itemNbt->at("Count") = ByteTag(1);
    auto itemPtr         = CT::NbtUtils::createItemFromNbt(*itemNbt);
    if (!itemPtr) {
        player.sendMessage(txt.getMessage("recycle.load_fail"));
        showCommissionDetailsForm(player, pos, dimId, region, commissionNbtStr);
        return;
    }
    ItemStack item = *itemPtr;

    int itemId = ItemRepository::getInstance().getOrCreateItemId(commissionNbtStr);
    if (itemId < 0) {
        player.sendMessage(txt.getMessage("recycle.item_id_fail"));
        showCommissionDetailsForm(player, pos, dimId, region, commissionNbtStr);
        return;
    }

    auto commission = RecycleService::getInstance().getCommission(pos, dimId, itemId);
    if (!commission) {
        player.sendMessage(txt.getMessage("recycle.no_commission"));
        showCommissionDetailsForm(player, pos, dimId, region, commissionNbtStr);
        return;
    }

    double currentPrice           = commission->price;
    int    currentMaxRecycleCount = commission->maxRecycleCount;

    ll::form::CustomForm fm;
    fm.setTitle("编辑回收委托");
    fm.appendLabel("物品: " + std::string(item.getName()) + " §7(" + item.getTypeName() + ")§r");
    fm.appendInput(
        "price_input",
        "回收单价",
        std::to_string(currentPrice),
        std::to_string(currentPrice)
    ); // 使用 std::to_string 显示 double
    fm.appendInput(
        "max_recycle_count",
        "最大回收数量 (0为不限制)",
        std::to_string(currentMaxRecycleCount),
        std::to_string(currentMaxRecycleCount)
    );

    fm.sendTo(
        player,
        [pos,
         dimId,
         commissionNbtStr,
         itemId](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason reason) {
            auto& region = p.getDimensionBlockSource();
            auto& txt    = TextService::getInstance();
            if (!result.has_value()) {
                p.sendMessage(txt.getMessage("action.cancelled"));
                showCommissionDetailsForm(p, pos, dimId, region, commissionNbtStr);
                return;
            }

            try {
                double newPrice = std::stod(std::get<std::string>(result.value().at("price_input")));
                if (newPrice < 0.0) {
                    p.sendMessage(txt.getMessage("input.negative_price"));
                    showEditCommissionForm(p, pos, dimId, region, commissionNbtStr);
                    return;
                }

                int newMaxRecycleCount = std::stoi(std::get<std::string>(result.value().at("max_recycle_count")));
                if (newMaxRecycleCount < 0) {
                    p.sendMessage(txt.getMessage("input.negative_max_count"));
                    showEditCommissionForm(p, pos, dimId, region, commissionNbtStr);
                    return;
                }

                if (RecycleService::getInstance().updateCommission(pos, dimId, itemId, newPrice, newMaxRecycleCount)) {
                    p.sendMessage(txt.getMessage(
                        "recycle.commission_update",
                        {
                            {"price", CT::MoneyFormat::format(newPrice) },
                            {"max",   std::to_string(newMaxRecycleCount)}
                    }
                    ));
                } else {
                    p.sendMessage(txt.getMessage("recycle.fail"));
                }

            } catch (const std::exception& e) {
                p.sendMessage(txt.getMessage("input.invalid_number"));
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
    auto& txt = TextService::getInstance();

    auto itemNbt = CT::NbtUtils::parseSNBT(commissionNbtStr);
    if (!itemNbt) {
        player.sendMessage(txt.getMessage("recycle.load_fail"));
        return;
    }
    itemNbt->at("Count") = ByteTag(1);
    auto itemPtr         = CT::NbtUtils::createItemFromNbt(*itemNbt);
    if (!itemPtr) {
        player.sendMessage(txt.getMessage("recycle.load_fail"));
        return;
    }
    std::string itemName = itemPtr->getName();

    auto& db = Sqlite3Wrapper::getInstance();

    std::string playerUuid = player.getUuid().asString();

    int itemId = ItemRepository::getInstance().getOrCreateItemId(commissionNbtStr);
    if (itemId < 0) {
        player.sendMessage(txt.getMessage("recycle.load_records_fail"));
        return;
    }

    logger.debug(
        "showCommissionDetailsForm: 开始异步查询回收记录 pos({},{},{}) dim {} itemId {}",
        pos.x,
        pos.y,
        pos.z,
        dimId,
        itemId
    );

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
        "SELECT price, max_recycle_count, current_recycled_count FROM recycle_shop_items WHERE dim_id = ? AND pos_x = "
        "? AND pos_y = ? AND pos_z = ? AND item_id = ?",
        dimId,
        pos.x,
        pos.y,
        pos.z,
        itemId
    );

    // 使用线程池等待查询完成，然后回调到主线程显示表单
    db.thenOnMainThread(
        std::move(recordsFuture),
        std::move(commissionFuture),
        [playerUuid, pos, dimId, itemName, commissionNbtStr](
            std::vector<std::vector<std::string>> records,
            std::vector<std::vector<std::string>> commissionInfo
        ) {
            logger.debug("showCommissionDetailsForm: 异步查询完成，记录数: {}", records.size());

            // 重新获取玩家对象
            auto* player = ll::service::getLevel()->getPlayer(mce::UUID::fromString(playerUuid));
            if (!player) {
                logger.warn("showCommissionDetailsForm: 玩家 {} 已离线，无法显示表单", playerUuid);
                return;
            }

            ll::form::SimpleForm fm;
            fm.setTitle("回收记录详情");

            std::string content = "物品: " + itemName + "\n\n";

            // 显示委托信息
            if (!commissionInfo.empty()) {
                double price                = std::stod(commissionInfo[0][0]);
                int    maxRecycleCount      = std::stoi(commissionInfo[0][1]);
                int    currentRecycledCount = std::stoi(commissionInfo[0][2]);

                content += "§6当前回收单价: " + CT::MoneyFormat::format(price) + "§r\n";
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
                    auto        playerInfo =
                        ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(recyclerUuid));
                    if (playerInfo) {
                        recyclerName = playerInfo->name;
                    }

                    content += "§f" + timestamp + " - " + recyclerName + " 回收了 " + recycleCount + " 个，花费 "
                             + totalPrice + " 金币\n";
                }
            }
            fm.setContent(content);

            fm.appendButton("§e编辑委托", [pos, dimId, commissionNbtStr](Player& p) {
                auto& region = p.getDimensionBlockSource();
                showEditCommissionForm(p, pos, dimId, region, commissionNbtStr);
            });

            fm.appendButton("返回", [pos, dimId](Player& p) {
                auto& region = p.getDimensionBlockSource();
                showViewRecycleCommissionsForm(p, pos, dimId, region);
            });

            fm.sendTo(*player);
        }
    );
}

void showViewRecycleCommissionsForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    // 异步查询回收委托列表
    auto& db = Sqlite3Wrapper::getInstance();

    // 获取玩家UUID用于后续回调
    std::string playerUuid = player.getUuid().asString();

    logger
        .debug("showViewRecycleCommissionsForm: 开始异步查询回收委托 pos({},{},{}) dim {}", pos.x, pos.y, pos.z, dimId);

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

    // 使用线程池等待查询完成，然后回调到主线程显示表单
    db.thenOnMainThread(std::move(future), [playerUuid, pos, dimId](std::vector<std::vector<std::string>> commissions) {
        logger.debug("showViewRecycleCommissionsForm: 异步查询完成，委托数: {}", commissions.size());

        // 重新获取玩家对象
        auto* player = ll::service::getLevel()->getPlayer(mce::UUID::fromString(playerUuid));
        if (!player) {
            logger.warn("showViewRecycleCommissionsForm: 玩家 {} 已离线，无法显示表单", playerUuid);
            return;
        }

        ll::form::SimpleForm fm;
        fm.setTitle("查看回收委托");

        if (commissions.empty()) {
            fm.setContent("该商店没有设置任何回收委托。");
        } else {
            fm.setContent("点击查看每个委托的详细回收记录：");
            for (const auto& row : commissions) {
                std::string itemNbtStr           = row[0];
                double      price                = std::stod(row[1]);
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
                    progress =
                        "§a[" + std::to_string(currentRecycledCount) + " / " + std::to_string(maxRecycleCount) + "]§r";
                }

                std::string buttonText = std::string(item.getName()) + " §e" + progress
                                       + " §6[单价: " + CT::MoneyFormat::format(price) + "]§r";
                fm.appendButton(buttonText, [pos, dimId, itemNbtStr](Player& p) {
                    auto& region = p.getDimensionBlockSource();
                    showCommissionDetailsForm(p, pos, dimId, region, itemNbtStr);
                });
            }
        }

        fm.appendButton("返回", [pos, dimId](Player& p) {
            auto& region = p.getDimensionBlockSource();
            showRecycleShopManageForm(p, pos, dimId, region);
        });

        fm.sendTo(*player);
    });
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
                fm.appendButton(buttonText, texturePath, "path", [pos, dimId, item](Player& p) {
                    auto& region = p.getDimensionBlockSource();
                    showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                });
            } else {
                fm.appendButton(buttonText, [pos, dimId, item](Player& p) {
                    auto& region = p.getDimensionBlockSource();
                    showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                });
            }
        }
    }

    fm.appendButton("返回", [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
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
        [item, pos, dimId](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason reason) {
            auto& region = p.getDimensionBlockSource();
            auto& txt    = TextService::getInstance();
            if (!result.has_value()) {
                p.sendMessage(txt.getMessage("action.cancelled"));
                showAddItemToRecycleShopForm(p, pos, dimId, region);
                return;
            }

            try {
                double price = std::stod(std::get<std::string>(result.value().at("price_input")));
                if (price < 0.0) {
                    p.sendMessage(txt.getMessage("input.negative_price"));
                    showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                    return;
                }

                int minDurability = 0;
                if (item.isDamageableItem()) {
                    minDurability = std::stoi(std::get<std::string>(result.value().at("min_durability")));
                    if (minDurability < 0) {
                        p.sendMessage(txt.getMessage("input.negative_durability"));
                        showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                        return;
                    }
                }

                std::string    requiredEnchantsStr = std::get<std::string>(result.value().at("required_enchants"));
                nlohmann::json enchantsJson        = nlohmann::json::array();
                if (!requiredEnchantsStr.empty()) {
                    std::stringstream ss(requiredEnchantsStr);
                    std::string       segment;
                    while (std::getline(ss, segment, ',')) {
                        std::stringstream segment_ss(segment);
                        std::string       id_str, level_str;
                        if (std::getline(segment_ss, id_str, ':') && std::getline(segment_ss, level_str)) {
                            try {
                                int            id    = std::stoi(id_str);
                                int            level = std::stoi(level_str);
                                nlohmann::json enchant;
                                enchant["id"]    = id;
                                enchant["level"] = level;
                                enchantsJson.push_back(enchant);
                            } catch (const std::exception& e) {
                                p.sendMessage(txt.getMessage("input.invalid_enchant"));
                                showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                                return;
                            }
                        }
                    }
                }
                std::string enchantsJsonStr = enchantsJson.is_null() || enchantsJson.empty() ? "" : enchantsJson.dump();

                int maxRecycleCount = std::stoi(std::get<std::string>(result.value().at("max_recycle_count")));
                if (maxRecycleCount < 0) {
                    p.sendMessage(txt.getMessage("input.negative_max_count"));
                    showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                    return;
                }

                auto itemNbt = CT::NbtUtils::getItemNbt(item);
                if (!itemNbt) {
                    p.sendMessage(txt.getMessage("input.nbt_fail"));
                    return;
                }

                auto        cleanedNbt = CT::NbtUtils::cleanNbtForComparison(*itemNbt);
                std::string itemNbtStr = CT::NbtUtils::toSNBT(*cleanedNbt);

                logger.info("Setting recycle commission for item '{}'.", item.getName());

                auto result =
                    RecycleService::getInstance()
                        .setCommission(pos, dimId, itemNbtStr, price, minDurability, enchantsJsonStr, maxRecycleCount);

                if (result.success) {
                    p.sendMessage(txt.getMessage(
                        "recycle.commission_set",
                        {
                            {"price", CT::MoneyFormat::format(price) },
                            {"max",   std::to_string(maxRecycleCount)}
                    }
                    ));
                } else {
                    p.sendMessage(txt.getMessage("recycle.fail"));
                }

            } catch (const std::exception& e) {
                p.sendMessage(txt.getMessage("input.invalid_number"));
                showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                return;
            }
            showRecycleShopManageForm(p, pos, dimId, region);
        }
    );
}


} // namespace CT
