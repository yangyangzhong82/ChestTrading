#include "RecycleForm.h"
#include "DynamicPricingForm.h"
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
#include "service/DynamicPricingService.h"
#include "service/RecycleService.h"
#include "service/TextService.h"


namespace CT {


void showRecycleForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    showRecycleItemListForm(player, pos, dimId, region);
}

void showRecycleItemListForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.recycle_shop_title"));

    auto commissions = RecycleService::getInstance().getCommissions(pos, dimId);

    if (commissions.empty()) {
        fm.setContent(txt.getMessage("recycle.empty"));
    } else {
        fm.setContent(txt.getMessage("recycle.list_content"));
        for (const auto& commission : commissions) {
            auto itemNbt = CT::NbtUtils::parseSNBT(commission.itemNbt);
            if (!itemNbt) {
                fm.appendButton(txt.getMessage("form.data_corrupt_button"), [](Player& p) {
                    p.sendMessage(TextService::getInstance().getMessage("recycle.data_corrupt"));
                });
                continue;
            }
            itemNbt->at("Count") = ByteTag(1);
            auto itemPtr         = CT::NbtUtils::createItemFromNbt(*itemNbt);
            if (!itemPtr) {
                fm.appendButton(txt.getMessage("form.data_corrupt_button2"), [](Player& p) {
                    p.sendMessage(TextService::getInstance().getMessage("recycle.data_corrupt"));
                });
                continue;
            }
            ItemStack item = *itemPtr;
            item.set(1);

            std::string buttonText = std::string(item.getName()) + " §7(" + item.getTypeName() + ")§r"
                                   + txt.getMessage(
                                       "form.recycle_price_label",
                                       {
                                           {"price", CT::MoneyFormat::format(commission.price)}
            }
                                   );

            std::string itemInfo;
            if (commission.requiredAuxValue >= 0) {
                itemInfo += txt.getMessage(
                    "form.recycle_require_aux",
                    {
                        {"value", std::to_string(commission.requiredAuxValue)}
                }
                );
            }
            if (commission.minDurability > 0) {
                itemInfo += txt.getMessage(
                    "form.recycle_require_durability",
                    {
                        {"value", std::to_string(commission.minDurability)}
                }
                );
            }
            if (!commission.requiredEnchants.empty()) {
                try {
                    nlohmann::json enchants = nlohmann::json::parse(commission.requiredEnchants);
                    if (enchants.is_array() && !enchants.empty()) {
                        itemInfo += txt.getMessage("form.recycle_require_enchant");
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

    fm.appendButton(txt.getMessage("form.button_back"), [pos, dimId](Player& p) {
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
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.recycle_confirm_title"));

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

    int            minDurability    = commission->minDurability;
    int            requiredAuxValue = commission->requiredAuxValue;
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
            // 检查特殊值（如箭的类型）
            if (requiredAuxValue >= 0 && itemInSlot.getAuxValue() != requiredAuxValue) {
                continue;
            }
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

    fm.appendLabel(txt.getMessage(
        "form.label_setting_commission",
        {
            {"item", std::string(item.getName()) + " §7(" + item.getTypeName() + ")§r"}
    }
    ));
    fm.appendLabel(txt.getMessage(
        "form.label_available_count",
        {
            {"count", std::to_string(totalPlayerCount)}
    }
    ));
    fm.appendLabel(txt.getMessage(
        "form.label_unit_price",
        {
            {"price", CT::MoneyFormat::format(unitPrice)}
    }
    ));


    // 显示耐久度
    if (item.isDamageableItem()) {
        int maxDamage = item.getItem()->getMaxDamage();
        fm.appendLabel(txt.getMessage(
            "form.durability_label",
            {
                {"current", std::to_string(maxDamage - item.getDamageValue())},
                {"max",     std::to_string(maxDamage)                        }
        }
        ));
    }

    // 显示特殊值
    short auxValue = item.getAuxValue();
    if (auxValue != 0) {
        fm.appendLabel(txt.getMessage(
            "form.aux_value_label",
            {
                {"value", std::to_string(auxValue)}
        }
        ));
    }

    // 获取并显示附魔信息
    if (item.isEnchanted()) {
        ItemEnchants enchants    = item.constructItemEnchantsFromUserData();
        auto         enchantList = enchants.getAllEnchants();
        if (!enchantList.empty()) {
            std::string enchantText = txt.getMessage("form.enchant_label");
            for (const auto& enchant : enchantList) {
                enchantText +=
                    NbtUtils::enchantToString(enchant.mEnchantType) + " " + std::to_string(enchant.mLevel) + " ";
            }
            fm.appendLabel(enchantText);
        }
    }

    fm.appendInput("recycle_count", txt.getMessage("form.input_recycle_count"), "1", std::to_string(totalPlayerCount));

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
    fm.setTitle(txt.getMessage("form.recycle_final_title"));

    std::string maxInfo = maxRecycleCount > 0 ? txt.getMessage(
                                                    "form.max_recycle_info",
                                                    {
                                                        {"max", std::to_string(maxRecycleCount)}
    }
                                                )
                                              : "";

    fm.setContent(txt.getMessage(
        "form.recycle_final_content",
        {
            {"item",     std::string(item.getName())          },
            {"count",    std::to_string(recycleCount)         },
            {"max_info", maxInfo                              },
            {"price",    CT::MoneyFormat::format(recyclePrice)}
    }
    ));

    fm.appendButton(
        txt.getMessage("form.button_confirm_recycle"),
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

    fm.appendButton(txt.getMessage("form.button_cancel"), [item, pos, dimId, unitPrice, commissionNbtStr](Player& p) {
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
    auto& txt = TextService::getInstance();
    CT::FormUtils::showSetNameForm(
        player,
        pos,
        dimId,
        txt.getMessage("form.set_recycle_name_title"),
        [](Player& p, BlockPos pos, int dimId) {
            auto& region = p.getDimensionBlockSource();
            showRecycleShopManageForm(p, pos, dimId, region);
        }
    );
}

void showRecycleShopManageForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.recycle_manage_title"));
    fm.setContent(txt.getMessage("form.recycle_manage_content"));

    fm.appendButton(txt.getMessage("form.button_add_commission"), [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showAddItemToRecycleShopForm(p, pos, dimId, region);
    });

    fm.appendButton(txt.getMessage("form.button_view_commission"), [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showViewRecycleCommissionsForm(p, pos, dimId, region);
    });

    fm.appendButton(txt.getMessage("form.button_back"), [pos, dimId](Player& p) {
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
    auto  itemNbt = CT::NbtUtils::parseSNBT(commissionNbtStr);
    auto& txt     = TextService::getInstance();
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
    fm.setTitle(txt.getMessage("form.recycle_edit_title"));
    fm.appendLabel(txt.getMessage(
        "form.label_item",
        {
            {"item", std::string(item.getName()) + " §7(" + item.getTypeName() + ")§r"}
    }
    ));
    fm.appendInput(
        "price_input",
        txt.getMessage("form.input_price_label"),
        std::to_string(currentPrice),
        std::to_string(currentPrice)
    );
    fm.appendInput(
        "max_recycle_count",
        txt.getMessage("form.input_max_recycle_label"),
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
            auto&                txt = TextService::getInstance();
            fm.setTitle(txt.getMessage("form.recycle_details_title"));

            std::string content = txt.getMessage("form.label_item_prefix") + itemName + "\n\n";

            // 显示委托信息
            if (!commissionInfo.empty()) {
                double price                = std::stod(commissionInfo[0][0]);
                int    maxRecycleCount      = std::stoi(commissionInfo[0][1]);
                int    currentRecycledCount = std::stoi(commissionInfo[0][2]);

                content += txt.getMessage(
                    "form.current_recycle_price",
                    {
                        {"price", CT::MoneyFormat::format(price)}
                }
                );
                if (maxRecycleCount > 0) {
                    content += txt.getMessage(
                        "form.max_recycle_count_label",
                        {
                            {"count", std::to_string(maxRecycleCount)}
                    }
                    );
                    content += txt.getMessage(
                        "form.current_recycled_count_label",
                        {
                            {"count", std::to_string(currentRecycledCount)}
                    }
                    );
                } else {
                    content += txt.getMessage("form.max_recycle_unlimited");
                }
            }

            if (records.empty()) {
                content += txt.getMessage("form.no_recycle_records");
            } else {
                content += txt.getMessage("form.recent_recycle_records");

                // 预先批量查询记录中所有玩家名称，避免 N+1 查询
                std::vector<std::string> uuids;
                for (const auto& row : records) {
                    uuids.push_back(row[0]);
                }
                auto ownerNameCache = CT::FormUtils::getPlayerNameCache(uuids);

                for (const auto& row : records) {
                    std::string recyclerUuid = row[0];
                    std::string recycleCount = row[1];
                    std::string totalPrice   = row[2];
                    std::string timestamp    = row[3];

                    std::string recyclerName = ownerNameCache[recyclerUuid];

                    content += txt.getMessage(
                        "form.recycle_record_format",
                        {
                            {"timestamp", timestamp   },
                            {"player",    recyclerName},
                            {"count",     recycleCount},
                            {"price",     totalPrice  }
                    }
                    );
                }
            }
            fm.setContent(content);

            fm.appendButton(txt.getMessage("form.button_edit_commission"), [pos, dimId, commissionNbtStr](Player& p) {
                auto& region = p.getDimensionBlockSource();
                showEditCommissionForm(p, pos, dimId, region, commissionNbtStr);
            });

            // 官方回收商店显示动态价格设置按钮
            auto& region    = player->getDimensionBlockSource();
            auto  chestInfo = ChestService::getInstance().getChestInfo(pos, dimId, region);
            if (chestInfo && chestInfo->type == ChestType::AdminRecycle) {
                int itemId = ItemRepository::getInstance().getOrCreateItemId(commissionNbtStr);
                if (itemId > 0) {
                    fm.appendButton(txt.getMessage("form.button_dynamic_pricing"), [pos, dimId, itemId](Player& p) {
                        showDynamicPricingForm(p, pos, dimId, itemId, false);
                    });
                }
            }

            fm.appendButton(txt.getMessage("form.button_back"), [pos, dimId](Player& p) {
                auto& region = p.getDimensionBlockSource();
                showViewRecycleCommissionsForm(p, pos, dimId, region);
            });

            fm.sendTo(*player);
        }
    );
}

void showViewRecycleCommissionsForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.recycle_view_title"));

    // 使用同步查询，与 showRecycleItemListForm 保持一致
    auto commissions = RecycleService::getInstance().getCommissions(pos, dimId);

    logger.debug("showViewRecycleCommissionsForm: 查询完成，委托数: {}", commissions.size());

    if (commissions.empty()) {
        fm.setContent(txt.getMessage("form.recycle_view_empty"));
    } else {
        fm.setContent(txt.getMessage("form.recycle_view_content"));
        for (const auto& commission : commissions) {
            auto itemNbt = CT::NbtUtils::parseSNBT(commission.itemNbt);
            if (!itemNbt) continue;
            itemNbt->at("Count") = ByteTag(1);
            auto itemPtr         = CT::NbtUtils::createItemFromNbt(*itemNbt);
            if (!itemPtr) continue;
            ItemStack item = *itemPtr;

            std::string progress = txt.getMessage("form.unlimited_label");
            if (commission.maxRecycleCount > 0) {
                progress = "§a[" + std::to_string(commission.currentRecycledCount) + " / "
                         + std::to_string(commission.maxRecycleCount) + "]§r";
            }

            std::string buttonText = std::string(item.getName()) + " §e" + progress
                                   + txt.getMessage(
                                       "form.price_tag",
                                       {
                                           {"price", CT::MoneyFormat::format(commission.price)}
            }
                                   );
            std::string itemNbtStr = commission.itemNbt;
            fm.appendButton(buttonText, [pos, dimId, itemNbtStr](Player& p) {
                auto& region = p.getDimensionBlockSource();
                showCommissionDetailsForm(p, pos, dimId, region, itemNbtStr);
            });
        }
    }

    fm.appendButton(txt.getMessage("form.button_back"), [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showRecycleShopManageForm(p, pos, dimId, region);
    });

    fm.sendTo(player);
}


void showAddItemByIdForm(Player& player, BlockPos pos, int dimId, BlockSource& region);

void showAddItemToRecycleShopForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.recycle_add_title"));
    fm.setContent(txt.getMessage("form.recycle_add_content"));

    // 首先添加"通过物品ID添加"按钮
    fm.appendButton(txt.getMessage("form.button_add_by_id"), [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showAddItemByIdForm(p, pos, dimId, region);
    });

    // 然后添加背包中的物品
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

    fm.appendButton(txt.getMessage("form.button_back"), [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showRecycleShopManageForm(p, pos, dimId, region);
    });

    fm.sendTo(player);
}

void showSetRecycleItemPriceForm(Player& player, const ItemStack& item, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::CustomForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.recycle_set_price_title"));
    fm.appendLabel(txt.getMessage(
        "form.label_setting_commission",
        {
            {"item", std::string(item.getName())}
    }
    ));
    fm.appendInput("price_input", txt.getMessage("form.input_price"), "0.0");

    // 显示当前物品的特殊值
    short currentAuxValue = item.getAuxValue();
    fm.appendLabel(txt.getMessage(
        "form.label_current_aux",
        {
            {"value", std::to_string(currentAuxValue)}
    }
    ));
    fm.appendInput("required_aux_value", txt.getMessage("form.input_aux_value"), "-1", std::to_string(currentAuxValue));

    if (item.isDamageableItem()) {
        fm.appendInput("min_durability", txt.getMessage("form.input_min_durability"), "0");
    }

    fm.appendInput("required_enchants", txt.getMessage("form.input_enchants"), "");
    fm.appendLabel(txt.getMessage("form.input_enchants_example"));
    fm.appendLabel(txt.getMessage("form.input_enchants_tip"));
    fm.appendInput("max_recycle_count", txt.getMessage("form.input_max_recycle"), "0");


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

                // 解析特殊值筛选
                int         requiredAuxValue = -1;
                std::string auxValueStr      = std::get<std::string>(result.value().at("required_aux_value"));
                if (!auxValueStr.empty()) {
                    try {
                        requiredAuxValue = std::stoi(auxValueStr);
                    } catch (...) {
                        requiredAuxValue = -1; // 解析失败则不筛选
                    }
                }

                auto itemNbt = CT::NbtUtils::getItemNbt(item);
                if (!itemNbt) {
                    p.sendMessage(txt.getMessage("input.nbt_fail"));
                    return;
                }

                auto        cleanedNbt = CT::NbtUtils::cleanNbtForComparison(*itemNbt);
                std::string itemNbtStr = CT::NbtUtils::toSNBT(*cleanedNbt);

                logger.info("Setting recycle commission for item '{}'.", item.getName());

                auto result = RecycleService::getInstance().setCommission(
                    pos,
                    dimId,
                    itemNbtStr,
                    price,
                    minDurability,
                    enchantsJsonStr,
                    maxRecycleCount,
                    requiredAuxValue
                );

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


void showAddItemByIdForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::CustomForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.recycle_add_by_id_title"));
    fm.appendLabel(txt.getMessage("form.label_item_id_hint"));
    fm.appendInput("item_id", txt.getMessage("form.input_item_id"), "", "");
    fm.appendLabel(txt.getMessage("form.label_item_id_tip1"));
    fm.appendLabel(txt.getMessage("form.label_item_id_tip2"));

    fm.sendTo(
        player,
        [pos, dimId](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason reason) {
            auto& region = p.getDimensionBlockSource();
            auto& txt    = TextService::getInstance();
            if (!result.has_value()) {
                p.sendMessage(txt.getMessage("action.cancelled"));
                showAddItemToRecycleShopForm(p, pos, dimId, region);
                return;
            }

            std::string itemIdStr = std::get<std::string>(result.value().at("item_id"));
            if (itemIdStr.empty()) {
                p.sendMessage(txt.getMessage("input.empty_item_id"));
                showAddItemByIdForm(p, pos, dimId, region);
                return;
            }

            // 处理物品ID：只有在没有命名空间前缀时才添加 minecraft:
            // addon物品通常格式为 namespace:item_name (如 mypack:custom_sword)
            std::string fullItemId = itemIdStr;
            if (fullItemId.find(':') == std::string::npos) {
                // 没有冒号，说明没有命名空间前缀，添加 minecraft:
                fullItemId = "minecraft:" + fullItemId;
            }

            // 尝试创建物品
            ItemStack item;
            item.reinit(fullItemId, 1, 0);

            if (item.isNull()) {
                p.sendMessage(txt.getMessage("input.invalid_item_id"));
                showAddItemByIdForm(p, pos, dimId, region);
                return;
            }

            // 跳转到设置价格表单
            showSetRecycleItemPriceForm(p, item, pos, dimId, region);
        }
    );
}


} // namespace CT
