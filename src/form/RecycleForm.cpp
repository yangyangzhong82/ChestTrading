#include "RecycleForm.h"
#include "DynamicPricingForm.h"
#include "FormUtils.h"
#include "LLMoney.h"
#include "LockForm.h"
#include "PlayerLimitForm.h"
#include "TradeRecordForm.h"
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

#include <algorithm>
#include <optional>
#include <vector>

namespace CT {

namespace {

struct DynamicRecyclePriceView {
    double unitPrice;
    bool   canTrade;
    int    remainingQuantity; // -1 means unlimited
};

struct RecycleListEntry {
    RecycleItemData commission;
    ItemStack       item;
    std::string     commissionNbtStr;
    std::string     texturePath;
    std::string     buttonText;
    double          displayUnitPrice{0.0};
    bool            completed{false};
};

DynamicRecyclePriceView getDynamicRecyclePriceView(
    BlockPos      pos,
    int           dimId,
    int           itemId,
    double        fallbackUnitPrice,
    BlockSource&  region
) {
    BlockPos mainPos = ChestService::getInstance().getMainChestPos(pos, region);
    auto     chestInfo = ChestService::getInstance().getChestInfo(mainPos, dimId, region);
    if (!chestInfo || chestInfo->type != ChestType::AdminRecycle) {
        return {fallbackUnitPrice, true, -1};
    }

    auto dpInfo = DynamicPricingService::getInstance().getPriceInfo(mainPos, dimId, itemId, false);
    if (!dpInfo) {
        return {fallbackUnitPrice, true, -1};
    }

    return {dpInfo->currentPrice, dpInfo->canTrade, dpInfo->remainingQuantity};
}

int getCommissionRemainingCount(const RecycleItemData& commission) {
    if (commission.maxRecycleCount <= 0) {
        return -1;
    }
    return std::max(0, commission.maxRecycleCount - commission.currentRecycledCount);
}

int countPlayerEligibleRecycleItems(
    Player&                player,
    const ItemStack&       item,
    const std::string&     commissionNbtStr,
    const RecycleItemData& commission
) {
    std::optional<std::string> commissionBinKey;
    if (auto commissionTag = CT::NbtUtils::parseSNBT(commissionNbtStr)) {
        auto cleaned = CT::NbtUtils::cleanNbtForComparison(*commissionTag, item.isDamageableItem());
        commissionBinKey = CT::NbtUtils::toBinaryNBT(*cleaned);
    }

    int            totalPlayerCount = 0;
    int            minDurability    = commission.minDurability;
    int            requiredAuxValue = commission.requiredAuxValue;
    nlohmann::json requiredEnchants;
    if (!commission.requiredEnchants.empty()) {
        try {
            requiredEnchants = nlohmann::json::parse(commission.requiredEnchants);
        } catch (const std::exception& e) {
            logger.error("Failed to parse required_enchants JSON in countPlayerEligibleRecycleItems: {}", e.what());
        }
    }

    for (int i = 0; i < player.getInventory().getContainerSize(); ++i) {
        const auto& itemInSlot = player.getInventory().getItem(i);
        if (itemInSlot.isNull()) continue;
        if (itemInSlot.getTypeName() != item.getTypeName()) continue;

        auto itemNbt = CT::NbtUtils::getItemNbt(itemInSlot);
        if (!itemNbt) continue;
        auto cleanedNbt = CT::NbtUtils::cleanNbtForComparison(*itemNbt, itemInSlot.isDamageableItem());

        bool matches = false;
        if (commissionBinKey) {
            matches = (CT::NbtUtils::toBinaryNBT(*cleanedNbt) == *commissionBinKey);
        } else {
            matches = (CT::NbtUtils::toSNBT(*cleanedNbt) == commissionNbtStr);
        }
        if (!matches) continue;

        if (requiredAuxValue >= 0 && itemInSlot.getAuxValue() != requiredAuxValue) {
            continue;
        }

        if (itemInSlot.isDamageableItem()) {
            int maxDamage         = itemInSlot.getItem()->getMaxDamage();
            int currentDamage     = itemInSlot.getDamageValue();
            int currentDurability = maxDamage - currentDamage;
            if (currentDurability < minDurability) continue;
        }

        if (!requiredEnchants.empty() && requiredEnchants.is_array()) {
            bool         allEnchantsMatch = true;
            ItemEnchants itemEnchants     = itemInSlot.constructItemEnchantsFromUserData();
            auto         allItemEnchants  = itemEnchants.getAllEnchants();
            for (const auto& reqEnchant : requiredEnchants) {
                bool found    = false;
                int  reqId    = reqEnchant["id"];
                int  reqLevel = reqEnchant["level"];
                for (const auto& itemEnchant : allItemEnchants) {
                    if ((int)itemEnchant.mEnchantType == reqId && itemEnchant.mLevel >= reqLevel) {
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

        totalPlayerCount += itemInSlot.mCount;
    }

    return totalPlayerCount;
}

} // namespace


void showRecycleForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    showRecycleItemListForm(player, pos, dimId, region);
}

void showRecycleItemListForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    auto&                txt = TextService::getInstance();
    std::string          title = txt.getMessage("form.recycle_shop_title");
    auto                 info  = ChestService::getInstance().getChestInfo(pos, dimId, region);
    if (info && !info->ownerUuid.empty()) {
        auto ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(info->ownerUuid));
        std::string ownerName = ownerInfo ? ownerInfo->name : txt.getMessage("public_shop.unknown_owner");
        title                 = txt.getMessage("form.recycle_shop_title_with_owner", {{"owner", ownerName}});
    }
    fm.setTitle(title);

    auto commissions = RecycleService::getInstance().getCommissions(pos, dimId);

    if (commissions.empty()) {
        fm.setContent(txt.getMessage("recycle.empty"));
    } else {
        fm.setContent(txt.getMessage("recycle.list_content"));
        std::vector<RecycleListEntry> entries;
        entries.reserve(commissions.size());

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
            auto   priceView        = getDynamicRecyclePriceView(pos, dimId, commission.itemId, commission.price, region);
            double displayUnitPrice = priceView.unitPrice;
            int    commissionRemaining = getCommissionRemainingCount(commission);
            bool   completed           = commissionRemaining == 0;

            std::string buttonText = std::string(item.getName()) + " §f(" + item.getTypeName() + ")§r\n";
            if (completed) {
                buttonText += txt.getMessage("form.recycle_completed_label");
            } else {
                buttonText += txt.getMessage(
                    "form.recycle_price_label",
                    {
                        {"price", CT::MoneyFormat::format(displayUnitPrice)}
                    }
                );
                if (commissionRemaining > 0) {
                    buttonText += txt.getMessage(
                        "form.recycle_remaining_limit",
                        {
                            {"count", std::to_string(commissionRemaining)}
                        }
                    );
                }
            }

            std::string itemInfo;
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

            entries.push_back(
                {
                    commission,
                    item,
                    commission.itemNbt,
                    CT::FormUtils::getItemTexturePath(item),
                    buttonText,
                    displayUnitPrice,
                    completed
                }
            );
        }

        std::stable_sort(entries.begin(), entries.end(), [](const RecycleListEntry& lhs, const RecycleListEntry& rhs) {
            return static_cast<int>(lhs.completed) < static_cast<int>(rhs.completed);
        });

        for (const auto& entry : entries) {
            if (entry.completed) {
                if (!entry.texturePath.empty()) {
                    fm.appendButton(
                        entry.buttonText,
                        entry.texturePath,
                        "path",
                        [pos, dimId, maxCount = entry.commission.maxRecycleCount](Player& p) {
                            auto& txt    = TextService::getInstance();
                            auto& region = p.getDimensionBlockSource();
                            p.sendMessage(txt.getMessage("recycle.max_reached", {{"max", std::to_string(maxCount)}}));
                            showRecycleForm(p, pos, dimId, region);
                        }
                    );
                } else {
                    fm.appendButton(entry.buttonText, [pos, dimId, maxCount = entry.commission.maxRecycleCount](Player& p) {
                        auto& txt    = TextService::getInstance();
                        auto& region = p.getDimensionBlockSource();
                        p.sendMessage(txt.getMessage("recycle.max_reached", {{"max", std::to_string(maxCount)}}));
                        showRecycleForm(p, pos, dimId, region);
                    });
                }
                continue;
            }

            if (!entry.texturePath.empty()) {
                fm.appendButton(
                    entry.buttonText,
                    entry.texturePath,
                    "path",
                    [pos, dimId, item = entry.item, price = entry.displayUnitPrice, commissionNbtStr = entry.commissionNbtStr](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showRecycleConfirmForm(p, item, pos, dimId, region, -1, price, commissionNbtStr);
                    }
                );
            } else {
                fm.appendButton(
                    entry.buttonText,
                    [pos, dimId, item = entry.item, price = entry.displayUnitPrice, commissionNbtStr = entry.commissionNbtStr](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showRecycleConfirmForm(p, item, pos, dimId, region, -1, price, commissionNbtStr);
                    }
                );
            }
        }
    }

    fm.appendButton(txt.getMessage("form.button_back"), "textures/ui/arrow_left", "path", [pos, dimId](Player& p) {
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
    int                actualSlotIndex,
    double             unitPrice,
    const std::string& commissionNbtStr
) {
    ll::form::CustomForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.recycle_confirm_title"));

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

    auto priceView = getDynamicRecyclePriceView(pos, dimId, itemId, unitPrice, region);
    if (!priceView.canTrade) {
        player.sendMessage(txt.getMessage("dynamic_pricing.recycle_stopped"));
        showRecycleForm(player, pos, dimId, region);
        return;
    }
    double displayUnitPrice = priceView.unitPrice;
    int totalPlayerCount = countPlayerEligibleRecycleItems(player, item, commissionNbtStr, *commission);
    int maxAllowedCount  = totalPlayerCount;

    int commissionRemaining = getCommissionRemainingCount(*commission);
    if (commissionRemaining == 0) {
        player.sendMessage(txt.getMessage("recycle.max_reached", {{"max", std::to_string(commission->maxRecycleCount)}}));
        showRecycleForm(player, pos, dimId, region);
        return;
    }
    if (commissionRemaining > 0) {
        maxAllowedCount = std::min(maxAllowedCount, commissionRemaining);
    }
    if (priceView.remainingQuantity != -1) {
        maxAllowedCount = std::min(maxAllowedCount, priceView.remainingQuantity);
    }

    fm.appendLabel(txt.getMessage(
        "form.label_setting_commission",
        {
            {"item", std::string(item.getName()) + " §f(" + item.getTypeName() + ")§r"}
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
            {"price", CT::MoneyFormat::format(displayUnitPrice)}
    }
    ));
    fm.appendLabel(txt.getMessage(
        "form.recycle_max_available_label",
        {
            {"count", std::to_string(std::max(0, maxAllowedCount))}
        }
    ));

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

    short auxValue = item.getAuxValue();
    if (auxValue != 0) {
        fm.appendLabel(txt.getMessage(
            "form.aux_value_label",
            {
                {"value", std::to_string(auxValue)}
        }
        ));
    }

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

    fm.appendInput(
        "recycle_count",
        txt.getMessage("form.input_recycle_count"),
        "1",
        std::to_string(std::max(1, maxAllowedCount))
    );

    fm.sendTo(
        player,
        [item, pos, dimId, actualSlotIndex, itemId, displayUnitPrice, commissionNbtStr, maxAllowedCount](
            Player&                           p,
            const ll::form::CustomFormResult& result,
            ll::form::FormCancelReason
        ) {
            auto& region = p.getDimensionBlockSource();
            auto& txt    = TextService::getInstance();
            if (!result.has_value()) {
                p.sendMessage(txt.getMessage("action.cancelled"));
                showRecycleForm(p, pos, dimId, region);
                return;
            }

            int recycleCount = 1;
            try {
                recycleCount = std::stoi(std::get<std::string>(result.value().at("recycle_count")));
                if (recycleCount <= 0 || recycleCount > maxAllowedCount) {
                    p.sendMessage(txt.getMessage(
                        "input.invalid_recycle_count",
                        {
                            {"max", std::to_string(maxAllowedCount)}
                    }
                    ));
                    showRecycleConfirmForm(
                        p,
                        item,
                        pos,
                        dimId,
                        region,
                        actualSlotIndex,
                        displayUnitPrice,
                        commissionNbtStr
                    );
                    return;
                }
            } catch (const std::exception&) {
                p.sendMessage(txt.getMessage("input.invalid_number"));
                showRecycleConfirmForm(
                    p,
                    item,
                    pos,
                    dimId,
                    region,
                    actualSlotIndex,
                    displayUnitPrice,
                    commissionNbtStr
                );
                return;
            }

            auto currentPriceView = getDynamicRecyclePriceView(pos, dimId, itemId, displayUnitPrice, region);
            if (!currentPriceView.canTrade) {
                p.sendMessage(txt.getMessage("dynamic_pricing.recycle_stopped"));
                showRecycleForm(p, pos, dimId, region);
                return;
            }
            if (currentPriceView.remainingQuantity != -1 && recycleCount > currentPriceView.remainingQuantity) {
                p.sendMessage(txt.getMessage(
                    "dynamic_pricing.recycle_exceed_limit",
                    {
                        {"remaining", std::to_string(currentPriceView.remainingQuantity)}
                }
                ));
                showRecycleConfirmForm(
                    p,
                    item,
                    pos,
                    dimId,
                    region,
                    actualSlotIndex,
                    currentPriceView.unitPrice,
                    commissionNbtStr
                );
                return;
            }

            double recyclePrice = getRecyclePrice(currentPriceView.unitPrice, recycleCount);

            showRecycleFinalConfirmForm(
                p,
                item,
                pos,
                dimId,
                region,
                recycleCount,
                recyclePrice,
                commissionNbtStr,
                currentPriceView.unitPrice
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
        [item, pos, dimId, recycleCount, commissionNbtStr, unitPrice](Player& p) {
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
                    {"price", CT::MoneyFormat::format(result.totalEarned)}
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

    fm.appendButton(txt.getMessage("form.button_view_records"), [pos, dimId](Player& p) {
        showRecycleShopTradeRecordsForm(p, pos, dimId, [pos, dimId](Player& backPlayer) {
            auto& regionRef = backPlayer.getDimensionBlockSource();
            showRecycleShopManageForm(backPlayer, pos, dimId, regionRef);
        });
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
            {"item", std::string(item.getName()) + " §f(" + item.getTypeName() + ")§r"}
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

                std::string newMaxRecycleCountStr = std::get<std::string>(result.value().at("max_recycle_count"));
                if (newMaxRecycleCountStr.find_first_not_of(" \t\r\n") == std::string::npos) {
                    p.sendMessage(txt.getMessage("input.empty_max_recycle_count"));
                    showEditCommissionForm(p, pos, dimId, region, commissionNbtStr);
                    return;
                }

                int newMaxRecycleCount = std::stoi(newMaxRecycleCountStr);
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
    BlockPos mainPos = ChestService::getInstance().getMainChestPos(pos, region);

    logger.debug(
        "showCommissionDetailsForm: 开始异步查询回收记录 pos({},{},{}) dim {} itemId {}",
        mainPos.x,
        mainPos.y,
        mainPos.z,
        dimId,
        itemId
    );

    // 异步查询回收记录
    auto recordsFuture = db.queryAsync(
        "SELECT recycler_uuid, recycle_count, total_price, timestamp FROM recycle_records WHERE dim_id = ? AND pos_x "
        "= ? AND pos_y = ? AND pos_z = ? AND item_id = ? ORDER BY timestamp DESC",
        dimId,
        mainPos.x,
        mainPos.y,
        mainPos.z,
        itemId
    );

    // 异步查询委托信息
    auto commissionFuture = db.queryAsync(
        "SELECT price, max_recycle_count, current_recycled_count FROM recycle_shop_items WHERE dim_id = ? AND pos_x = "
        "? AND pos_y = ? AND pos_z = ? AND item_id = ?",
        dimId,
        mainPos.x,
        mainPos.y,
        mainPos.z,
        itemId
    );

    // 使用线程池等待查询完成，然后回调到主线程显示表单
    db.thenOnMainThread(
        std::move(recordsFuture),
        std::move(commissionFuture),
        [playerUuid, mainPos, dimId, itemName, commissionNbtStr](
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
                auto&  region               = player->getDimensionBlockSource();
                int    itemId               = ItemRepository::getInstance().getOrCreateItemId(commissionNbtStr);
                double displayPrice         = price;
                if (itemId > 0) {
                    auto priceView = getDynamicRecyclePriceView(mainPos, dimId, itemId, price, region);
                    displayPrice   = priceView.unitPrice;
                }

                content += txt.getMessage(
                    "form.current_recycle_price",
                    {
                        {"price", CT::MoneyFormat::format(displayPrice)}
                }
                );
                if (maxRecycleCount > 0) {
                    content += txt.getMessage(
                        "form.max_recycle_count_label",
                        {
                            {"count", std::to_string(maxRecycleCount)}
                    }
                    );
                } else {
                    content += txt.getMessage("form.max_recycle_unlimited");
                }
                content += txt.getMessage(
                    "form.current_recycled_count_label",
                    {
                        {"count", std::to_string(currentRecycledCount)}
                }
                );
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

            fm.appendButton(txt.getMessage("form.button_edit_commission"), [mainPos, dimId, commissionNbtStr](Player& p) {
                auto& region = p.getDimensionBlockSource();
                showEditCommissionForm(p, mainPos, dimId, region, commissionNbtStr);
            });

            fm.appendButton(txt.getMessage("form.button_remove_commission"), [mainPos, dimId, commissionNbtStr](Player& p) {
                auto& region = p.getDimensionBlockSource();
                auto& txt    = TextService::getInstance();
                int   itemId = ItemRepository::getInstance().getOrCreateItemId(commissionNbtStr);
                if (itemId < 0) {
                    p.sendMessage(txt.getMessage("recycle.item_id_fail"));
                    showCommissionDetailsForm(p, mainPos, dimId, region, commissionNbtStr);
                    return;
                }

                if (RecycleService::getInstance().removeCommission(mainPos, dimId, itemId, region)) {
                    p.sendMessage(txt.getMessage("recycle.commission_removed"));
                } else {
                    p.sendMessage(txt.getMessage("recycle.commission_remove_fail"));
                }
                showViewRecycleCommissionsForm(p, mainPos, dimId, region);
            });

            int limitItemId = ItemRepository::getInstance().getOrCreateItemId(commissionNbtStr);
            if (limitItemId > 0) {
                fm.appendButton(
                    txt.getMessage("form.button_item_limit"),
                    [mainPos, dimId, limitItemId, itemName](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showPlayerItemLimitForm(p, mainPos, dimId, region, false, limitItemId, itemName);
                    }
                );
            }

            // 官方回收商店显示动态价格设置按钮
            auto& region    = player->getDimensionBlockSource();
            auto  chestInfo = ChestService::getInstance().getChestInfo(mainPos, dimId, region);
            if (chestInfo && chestInfo->type == ChestType::AdminRecycle) {
                int itemId = ItemRepository::getInstance().getOrCreateItemId(commissionNbtStr);
                if (itemId > 0) {
                    fm.appendButton(txt.getMessage("form.button_dynamic_pricing"), [mainPos, dimId, itemId](Player& p) {
                        showDynamicPricingForm(p, mainPos, dimId, itemId, false);
                    });
                }
            }

            fm.appendButton(txt.getMessage("form.button_back"), [mainPos, dimId](Player& p) {
                auto& region = p.getDimensionBlockSource();
                showViewRecycleCommissionsForm(p, mainPos, dimId, region);
            });

            fm.sendTo(*player);
        }
    );
}

void showViewRecycleCommissionsForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.recycle_view_title"));
    BlockPos mainPos = ChestService::getInstance().getMainChestPos(pos, region);

    // 使用同步查询，与 showRecycleItemListForm 保持一致
    auto commissions = RecycleService::getInstance().getCommissions(mainPos, dimId);

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

            std::string progress = txt.getMessage(
                "form.current_recycled_inline_label",
                {
                    {"count", std::to_string(commission.currentRecycledCount)}
            }
            );
            if (commission.maxRecycleCount > 0) {
                progress = "§a[" + std::to_string(commission.currentRecycledCount) + " / "
                         + std::to_string(commission.maxRecycleCount) + "]§r";
            }

            auto   priceView        = getDynamicRecyclePriceView(mainPos, dimId, commission.itemId, commission.price, region);
            double displayUnitPrice = priceView.unitPrice;

            std::string buttonText = std::string(item.getName()) + " §e" + progress
                                   + txt.getMessage(
                                       "form.price_tag",
                                       {
                                           {"price", CT::MoneyFormat::format(displayUnitPrice)}
            }
                                   );
            std::string itemNbtStr  = commission.itemNbt;
            std::string texturePath = CT::FormUtils::getItemTexturePath(item);
            if (!texturePath.empty()) {
                fm.appendButton(buttonText, texturePath, "path", [mainPos, dimId, itemNbtStr](Player& p) {
                    auto& region = p.getDimensionBlockSource();
                    showCommissionDetailsForm(p, mainPos, dimId, region, itemNbtStr);
                });
            } else {
                fm.appendButton(buttonText, [mainPos, dimId, itemNbtStr](Player& p) {
                    auto& region = p.getDimensionBlockSource();
                    showCommissionDetailsForm(p, mainPos, dimId, region, itemNbtStr);
                });
            }
        }
    }

    fm.appendButton(txt.getMessage("form.button_back"), [mainPos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        auto  info   = ChestService::getInstance().getChestInfo(mainPos, dimId, region);
        CT::showChestLockForm(
            p,
            mainPos,
            dimId,
            info.has_value(),
            info ? info->ownerUuid : "",
            info ? info->type : ChestType::Invalid,
            region
        );
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
                std::string(item.getName()) + " §f(" + item.getTypeName() + ")§r x" + std::to_string(item.mCount);
            std::string texturePath = CT::FormUtils::getItemTexturePath(item);

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

                std::string maxRecycleCountStr = std::get<std::string>(result.value().at("max_recycle_count"));
                if (maxRecycleCountStr.find_first_not_of(" \t\r\n") == std::string::npos) {
                    p.sendMessage(txt.getMessage("input.empty_max_recycle_count"));
                    showSetRecycleItemPriceForm(p, item, pos, dimId, region);
                    return;
                }

                int maxRecycleCount = std::stoi(maxRecycleCountStr);
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

                auto cleanedNbt = CT::NbtUtils::cleanNbtForComparison(*itemNbt, item.isDamageableItem());
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
                    requiredAuxValue,
                    p.getUuid().asString()
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
            auto info = ChestService::getInstance().getChestInfo(pos, dimId, region);
            CT::showChestLockForm(
                p,
                pos,
                dimId,
                info.has_value(),
                info ? info->ownerUuid : "",
                info ? info->type : ChestType::Invalid,
                region
            );
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
