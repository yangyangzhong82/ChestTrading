#include "ShopForm.h"
#include "Config/ConfigManager.h"
#include "DynamicPricingForm.h"
#include "FormUtils.h"
#include "LockForm.h"
#include "PlayerLimitForm.h"
#include "TradeRecordForm.h"
#include "Utils/MoneyFormat.h"
#include "Utils/NbtUtils.h"
#include "Utils/economy.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/PlayerInfo.h"
#include "mc/platform/UUID.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "repository/ItemRepository.h"
#include "repository/ShopRepository.h"
#include "service/ChestService.h"
#include "service/DynamicPricingService.h"
#include "service/ShopService.h"
#include "service/TeleportService.h"
#include "service/TextService.h"

#include <algorithm>
#include <cctype>

namespace CT {

namespace {

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trimCopy(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    auto end   = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

bool itemMatchesKeyword(const ItemStack& item, const std::string& keyword) {
    if (keyword.empty()) {
        return true;
    }

    const std::string lowerKeyword = toLowerCopy(keyword);
    const std::string itemName     = toLowerCopy(std::string(item.getName()));
    const std::string typeName     = toLowerCopy(item.getTypeName());
    return itemName.find(lowerKeyword) != std::string::npos || typeName.find(lowerKeyword) != std::string::npos;
}

void showShopItemSearchForm(Player& player, BlockPos pos, int dimId, const std::string& currentKeyword) {
    ll::form::CustomForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("public_items.search_title"));
    fm.appendInput(
        "keyword",
        txt.getMessage("public_items.search_keyword"),
        txt.getMessage("public_items.search_hint"),
        currentKeyword
    );

    fm.sendTo(
        player,
        [pos, dimId, currentKeyword](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
            auto& region = p.getDimensionBlockSource();
            if (!result.has_value()) {
                showShopChestItemsForm(p, pos, dimId, region, currentKeyword);
                return;
            }

            const auto keywordIt = result->find("keyword");
            std::string keyword;
            if (keywordIt != result->end()) {
                if (const auto* value = std::get_if<std::string>(&keywordIt->second)) {
                    keyword = trimCopy(*value);
                }
            }
            showShopChestItemsForm(p, pos, dimId, region, keyword);
        }
    );
}

} // namespace

void showShopChestItemsForm(Player& player, BlockPos pos, int dimId, BlockSource& region, const std::string& searchKeyword) {
    ll::form::SimpleForm fm;
    auto&                txt = TextService::getInstance();

    std::string title = txt.getMessage("form.shop_items_title");
    auto        info  = ChestService::getInstance().getChestInfo(pos, dimId, region);
    if (info && !info->ownerUuid.empty()) {
        auto ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(info->ownerUuid));
        std::string ownerName = ownerInfo ? ownerInfo->name : txt.getMessage("public_shop.unknown_owner");
        title                 = txt.getMessage("form.shop_items_title_with_owner", {{"owner", ownerName}});
    }
    fm.setTitle(title);

    logger.debug(
        "showShopChestItemsForm: Player {} is opening shop at pos ({},{},{}) dim {}.",
        player.getRealName(),
        pos.x,
        pos.y,
        pos.z,
        dimId
    );

    auto items = ShopService::getInstance().getShopItems(pos, dimId, region);
    int  matchedItemCount = 0;

    fm.appendButton(
        txt.getMessage("public_shop.button_search"),
        "textures/ui/magnifyingGlass",
        "path",
        [pos, dimId, searchKeyword](Player& p) { showShopItemSearchForm(p, pos, dimId, searchKeyword); }
    );

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
                fm.appendButton(txt.getMessage("form.data_corrupt_button"), [&txt](Player& p) {
                    p.sendMessage(txt.getMessage("shop.data_corrupt"));
                });
                continue;
            }
            ItemStack item = *itemPtr;
            item.set(1);
            if (!itemMatchesKeyword(item, searchKeyword)) {
                continue;
            }
            ++matchedItemCount;

            int         totalCount = ShopService::getInstance().countItemsInChest(region, pos, dimId, itemNbtStr);
            std::string buttonText =
                txt.generateShopItemText(item.getName(), shopItem.price, shopItem.dbCount, totalCount);
            std::string texturePath = CT::FormUtils::getItemTexturePath(item);

            if (!texturePath.empty()) {
                fm.appendButton(
                    buttonText,
                    texturePath,
                    "path",
                    [pos, dimId, item, itemNbtStr, unitPrice = shopItem.price, searchKeyword](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showShopItemBuyForm(p, item, pos, dimId, 0, unitPrice, region, itemNbtStr, searchKeyword);
                    }
                );
            } else {
                fm.appendButton(
                    buttonText,
                    [pos, dimId, item, itemNbtStr, unitPrice = shopItem.price, searchKeyword](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showShopItemBuyForm(p, item, pos, dimId, 0, unitPrice, region, itemNbtStr, searchKeyword);
                    }
                );
            }
        }

        if (!searchKeyword.empty()) {
            std::string content = txt.getMessage("public_items.search_result", {{"keyword", searchKeyword}});
            if (matchedItemCount == 0) {
                content = txt.getMessage("public_items.no_match") + content;
            }
            fm.setContent(content);
        }
    }

    fm.appendButton(txt.getMessage("form.button_back"), "textures/ui/arrow_left", "path", [pos, dimId](Player& p) {
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

void showPlayerPurchaseHistoryForm(Player& player, std::function<void(Player&)> onBack) {
    showPlayerTradeRecordsForm(player, onBack);
}

void showShopItemPriceForm(Player& player, const ItemStack& item, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::CustomForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.shop_set_price_title"));
    fm.appendLabel(txt.getMessage(
        "form.label_setting_price",
        {
            {"item", std::string(item.getName())}
    }
    ));
    fm.appendInput("price_input", txt.getMessage("form.input_price"), "0.0");

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
                    p.sendMessage(txt.getMessage("input.nbt_fail"));
                    return;
                }
                std::string itemNbtStr = CT::NbtUtils::toSNBT(*CT::NbtUtils::cleanNbtForComparison(*itemNbt));
                int         dbCount    = ShopService::getInstance().countItemsInChest(region, pos, dimId, itemNbtStr);

                auto priceResult =
                    ShopService::getInstance().setItemPrice(pos, dimId, itemNbtStr, price, dbCount, region);
                p.sendMessage(priceResult.message);
            } catch (const std::exception& e) {
                p.sendMessage(txt.getMessage("input.invalid_number"));
                logger.error("showShopItemPriceForm: 璁剧疆鐗╁搧浠锋牸鏃跺彂鐢熼敊璇? {}", e.what());
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
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.shop_manage_item_title"));

    auto itemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbtStr);
    if (!itemPtr) {
        player.sendMessage(txt.getMessage("shop.item_manage_fail"));
        showShopChestManageForm(player, pos, dimId, region);
        return;
    }
    ItemStack item = *itemPtr;

    int totalCount = ShopService::getInstance().countItemsInChest(region, pos, dimId, itemNbtStr);
    int itemId     = ItemRepository::getInstance().getOrCreateItemId(itemNbtStr);
    if (itemId < 0) {
        player.sendMessage(txt.getMessage("shop.item_id_fail"));
        showShopChestManageForm(player, pos, dimId, region);
        return;
    }

    auto itemOpt = ShopRepository::getInstance().findItem(pos, dimId, itemId);

    std::string content = txt.getMessage(
                              "form.label_managing_item",
                              {
                                  {"item", std::string(item.getName())}
    }
                          )
                        + "\n";
    int dbCount = 0;
    if (itemOpt) {
        content += txt.getMessage(
                       "form.label_current_price",
                       {
                           {"price", CT::MoneyFormat::format(itemOpt->price)}
        }
                   )
                 + "\n";
        dbCount = itemOpt->dbCount;
    } else {
        content += txt.getMessage("form.label_not_priced") + "\n";
    }
    content += txt.getMessage(
                   "form.label_db_stock",
                   {
                       {"count", std::to_string(dbCount)}
    }
               )
             + "\n";
    content += txt.getMessage(
                   "form.label_chest_stock",
                   {
                       {"count", std::to_string(totalCount)}
    }
               )
             + "\n";
    fm.setContent(content);

    fm.appendButton(txt.getMessage("form.button_set_price"), [item, pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showShopItemPriceForm(p, item, pos, dimId, region);
    });

    fm.appendButton(txt.getMessage("form.button_item_limit"), [pos, dimId, itemId, itemName = std::string(item.getName())](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showPlayerItemLimitForm(p, pos, dimId, region, true, itemId, itemName);
    });

    // 官方商店显示动态价格设置按钮
    auto chestInfo = ChestService::getInstance().getChestInfo(pos, dimId, region);
    if (chestInfo && chestInfo->type == ChestType::AdminShop) {
        fm.appendButton(txt.getMessage("form.button_dynamic_pricing"), [pos, dimId, itemId](Player& p) {
            showDynamicPricingForm(p, pos, dimId, itemId, true);
        });
    }

    fm.appendButton(txt.getMessage("form.button_remove_item"), [pos, dimId, itemId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        auto& txt    = TextService::getInstance();
        if (ShopService::getInstance().removeItem(pos, dimId, itemId, region)) {
            p.sendMessage(txt.getMessage("shop.item_removed"));
        } else {
            p.sendMessage(txt.getMessage("shop.item_remove_fail"));
        }
        showShopChestManageForm(p, pos, dimId, region);
    });

    fm.appendButton(txt.getMessage("form.button_back"), [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showShopChestManageForm(p, pos, dimId, region);
    });

    fm.sendTo(player);
}


void showPurchaseRecordsForm(Player& player, BlockPos pos, int dimId, BlockSource& region);

void showSetShopNameForm(Player& player, BlockPos pos, int dimId, BlockSource& region);

void showShopChestManageForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    ll::form::SimpleForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.shop_manage_title"));

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
        player.sendMessage(txt.getMessage("chest.entity_fail"));
        logger.error(
            "showShopChestManageForm: 鏃犳硶鑾峰彇绠卞瓙瀹炰綋鎴栫被鍨嬩笉鍖归厤鍦?({}, {}, {}) in dim {}",
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
                logger.error("showShopChestManageForm: Failed to get NBT data for slot {}.", i);
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
                std::string priceStr = txt.getMessage("form.label_not_priced");
                if (itemId > 0) {
                    auto itemOpt = ShopRepository::getInstance().findItem(pos, dimId, itemId);
                    if (itemOpt) {
                        priceStr = txt.getMessage(
                            "form.label_priced",
                            {
                                {"price", CT::MoneyFormat::format(itemOpt->price)}
                        }
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

        std::string buttonText  = CT::FormUtils::getItemDisplayString(item, totalCount, false) + "\n" + priceStr;
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
        fm.setContent(txt.getMessage("shop.chest_empty"));
        logger.debug("showShopChestManageForm: Chest at pos ({},{},{}) dim {} is empty.", pos.x, pos.y, pos.z, dimId);
    }

    fm.appendButton(txt.getMessage("form.button_view_records"), [pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showPurchaseRecordsForm(p, pos, dimId, region);
    });

    fm.appendButton(txt.getMessage("form.button_back"), [pos, dimId](Player& p) {
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
    double             unitPrice, // 淇敼涓?double
    BlockSource&       region,
    const std::string& itemNbtStr, // 娣诲姞 itemNbtStr 鍙傛暟
    const std::string& searchKeyword
) {
    ll::form::CustomForm fm;
    auto&                txt = TextService::getInstance();
    fm.setTitle(txt.getMessage("form.shop_buy_title"));

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

    fm.appendLabel(txt.getMessage(
        "form.label_buying_item",
        {
            {"item", CT::FormUtils::getItemDisplayString(item, 0, true)}
    }
    ));

    int itemId = ItemRepository::getInstance().getOrCreateItemId(itemNbtStr);
    if (itemId > 0) {
        auto itemOpt = ShopRepository::getInstance().findItem(pos, dimId, itemId);
        if (itemOpt) {
            fm.appendLabel(txt.getMessage(
                "form.remaining_stock",
                {
                    {"stock", std::to_string(itemOpt->dbCount)}
            }
            ));
        }
    }

    fm.appendLabel(txt.getMessage(
        "form.label_unit_price",
        {
            {"price", CT::MoneyFormat::format(unitPrice)}
    }
    ));
    fm.appendInput("buy_count", txt.getMessage("form.input_buy_count"), "1");
    fm.appendLabel(txt.getMessage(
        "form.label_your_balance",
        {
            {"balance", CT::MoneyFormat::format(Economy::getMoney(player))}
    }
    ));

    fm.sendTo(
        player,
        [item, pos, dimId, slot, unitPrice, itemNbtStr, searchKeyword](
            Player&                           p,
            const ll::form::CustomFormResult& result,
            ll::form::FormCancelReason        reason
        ) {
            auto& region = p.getDimensionBlockSource();
            auto& txt    = TextService::getInstance();
            if (!result.has_value()) {
                logger.debug("showShopItemBuyForm: Player {} cancelled purchase.", p.getRealName());
                p.sendMessage(txt.getMessage("action.cancelled"));
                showShopChestItemsForm(p, pos, dimId, region, searchKeyword);
                return;
            }

            int buyCount = 1;
            try {
                const auto& buyCountVal = result.value().at("buy_count");
                std::string buyCountStr;
                if (std::holds_alternative<std::string>(buyCountVal)) {
                    buyCountStr = std::get<std::string>(buyCountVal);
                } else if (std::holds_alternative<uint64>(buyCountVal)) {
                    buyCount = static_cast<int>(std::get<uint64>(buyCountVal));
                } else if (std::holds_alternative<double>(buyCountVal)) {
                    buyCount = static_cast<int>(std::get<double>(buyCountVal));
                }
                if (!buyCountStr.empty()) {
                    buyCount = std::stoi(buyCountStr);
                }
                logger.debug("showShopItemBuyForm: Player {} entered buyCount {}.", p.getRealName(), buyCount);
                if (buyCount <= 0) {
                    p.sendMessage(txt.getMessage("input.invalid_count"));
                    logger
                        .warn("showShopItemBuyForm: Player {} entered invalid buyCount {}.", p.getRealName(), buyCount);
                    showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr, searchKeyword);
                    return;
                }
            } catch (const std::exception& e) {
                p.sendMessage(txt.getMessage("input.invalid_buy_count"));
                logger
                    .error("showShopItemBuyForm: Error parsing buyCount for player {}: {}", p.getRealName(), e.what());
                showShopItemBuyForm(p, item, pos, dimId, slot, unitPrice, region, itemNbtStr, searchKeyword);
                return;
            }

            int itemId = ItemRepository::getInstance().getOrCreateItemId(itemNbtStr);
            if (itemId < 0) {
                p.sendMessage(txt.getMessage("shop.purchase_id_fail"));
                logger.error("showShopItemBuyForm: Failed to get or create item_id for item NBT {}.", itemNbtStr);
                showShopChestItemsForm(p, pos, dimId, region, searchKeyword);
                return;
            }

            auto purchaseResult =
                ShopService::getInstance().purchaseItem(p, pos, dimId, itemId, buyCount, region, itemNbtStr);
            p.sendMessage(purchaseResult.message);
            showShopChestItemsForm(p, pos, dimId, region, searchKeyword);
        }
    );
}

void showSetShopNameForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    auto& txt = TextService::getInstance();
    CT::FormUtils::showSetNameForm(
        player,
        pos,
        dimId,
        txt.getMessage("form.shop_set_name_title"),
        [](Player& p, BlockPos pos, int dimId) {
            auto& region = p.getDimensionBlockSource();
            showShopChestManageForm(p, pos, dimId, region);
        }
    );
}

void showPurchaseRecordsForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    (void)region;
    showShopTradeRecordsForm(player, pos, dimId, [pos, dimId](Player& p) {
        auto& regionRef = p.getDimensionBlockSource();
        showShopChestManageForm(p, pos, dimId, regionRef);
    });
}

} // namespace CT
