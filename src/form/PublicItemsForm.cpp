#include "PublicItemsForm.h"
#include "FormUtils.h"
#include "ShopForm.h"
#include "TradeRecordForm.h"
#include "Utils/MoneyFormat.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/PlayerInfo.h"
#include "mc/platform/UUID.h"
#include "repository/ShopRepository.h"
#include "service/I18nService.h"
#include <algorithm>
#include <cctype>

namespace CT {

static const int ITEMS_PER_PAGE = 10;

// 前向声明
static void showShopItemDetailForm(Player& player, const PublicShopItemData& item);
static void showRecycleItemDetailForm(Player& player, const PublicRecycleItemData& item);

static std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

static bool fuzzyMatch(const std::string& text, const std::string& keyword) {
    return toLower(text).find(toLower(keyword)) != std::string::npos;
}

static void showSearchForm(Player& player);

void showPublicItemsForm(Player& player, int currentPage, const std::string& searchKeyword) {
    auto& i18n = I18nService::getInstance();

    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get("public_items.list_title"));

    auto allItems = ShopRepository::getInstance().findAllPublicShopItems();

    // 筛选物品
    std::vector<PublicShopItemData> filteredItems;
    for (const auto& item : allItems) {
        if (item.dbCount <= 0) continue;
        if (!searchKeyword.empty()) {
            auto itemPtr = CT::FormUtils::createItemStackFromNbtString(item.itemNbt);
            if (!itemPtr) continue;
            std::string itemName = itemPtr->getName();
            std::string typeName = itemPtr->getTypeName();
            if (!fuzzyMatch(itemName, searchKeyword) && !fuzzyMatch(typeName, searchKeyword)) {
                continue;
            }
        }
        filteredItems.push_back(item);
    }

    int totalItems = static_cast<int>(filteredItems.size());
    int totalPages = (totalItems + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;
    currentPage = std::max(0, std::min(currentPage, totalPages - 1));
    fm.appendButton(i18n.get("public_shop.button_search"), "textures/ui/magnifyingGlass", "path", [](Player& p) {
        showSearchForm(p);
    });
    fm.appendButton(
        i18n.get("form.button_trade_records"),
        "textures/ui/book_edit_default",
        "path",
        [currentPage, searchKeyword](Player& p) {
            showTradeRecordMenuForm(p, [currentPage, searchKeyword](Player& playerToBack) {
                showPublicItemsForm(playerToBack, currentPage, searchKeyword);
            }, false);
        }
    );

    if (filteredItems.empty()) {
        fm.setContent(searchKeyword.empty() ? i18n.get("public_items.no_items") : i18n.get("public_items.no_match"));
    } else {
        std::string contentText = i18n.get(
            "public_items.total_items",
            {
                {"count", std::to_string(totalItems)     },
                {"page",  std::to_string(currentPage + 1)},
                {"total", std::to_string(totalPages)     }
        }
        );
        if (!searchKeyword.empty()) {
            contentText += i18n.get(
                "public_items.search_result",
                {
                    {"keyword", searchKeyword}
            }
            );
        }
        fm.setContent(contentText);

        int startIdx = currentPage * ITEMS_PER_PAGE;
        int endIdx   = std::min(startIdx + ITEMS_PER_PAGE, totalItems);

        // 批量获取店主名称
        std::vector<std::string> uuids;
        for (int i = startIdx; i < endIdx; ++i) {
            uuids.push_back(filteredItems[i].ownerUuid);
        }
        auto ownerNameCache = CT::FormUtils::getPlayerNameCache(uuids);

        for (int i = startIdx; i < endIdx; ++i) {
            const auto& item    = filteredItems[i];
            auto        itemPtr = CT::FormUtils::createItemStackFromNbtString(item.itemNbt);
            if (!itemPtr) continue;

            const std::string& ownerName       = ownerNameCache[item.ownerUuid];
            std::string        shopDisplayName = item.shopName.empty() ? i18n.get(
                                                                      "public_shop.owner_shop",
                                                                      {
                                                                          {"owner", ownerName}
            }
                                                                  )
                                                                       : item.shopName;

            std::string officialTag = item.isOfficial ? i18n.get("public_shop.official_tag") + " " : "";

            std::string buttonText = officialTag + "§b" + std::string(itemPtr->getName()) + "§r §6["
                                   + CT::MoneyFormat::format(item.price) + "]§r" + "\n§f" + shopDisplayName;

            std::string texturePath = CT::FormUtils::getItemTexturePath(*itemPtr);

            if (!texturePath.empty()) {
                fm.appendButton(buttonText, texturePath, "path", [item](Player& p) {
                    showShopItemDetailForm(p, item);
                });
            } else {
                fm.appendButton(buttonText, [item](Player& p) { showShopItemDetailForm(p, item); });
            }
        }
    }

    // 分页按钮
    if (totalPages > 1) {
        if (currentPage > 0) {
            fm.appendButton(
                i18n.get("public_shop.button_prev_page"),
                "textures/ui/arrow_left",
                "path",
                [currentPage, searchKeyword](Player& p) { showPublicItemsForm(p, currentPage - 1, searchKeyword); }
            );
        }
        if (currentPage < totalPages - 1) {
            fm.appendButton(
                i18n.get("public_shop.button_next_page"),
                "textures/ui/arrow_right",
                "path",
                [currentPage, searchKeyword](Player& p) { showPublicItemsForm(p, currentPage + 1, searchKeyword); }
            );
        }
    }

    fm.appendButton(i18n.get("public_shop.button_close"), "textures/ui/cancel", "path", [](Player& p) {});
    fm.sendTo(player);
}

static void showSearchForm(Player& player) {
    auto&                i18n = I18nService::getInstance();
    ll::form::CustomForm fm;
    fm.setTitle(i18n.get("public_items.search_title"));
    fm.appendInput("keyword", i18n.get("public_items.search_keyword"), i18n.get("public_items.search_hint"));

    fm.sendTo(player, [](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result.has_value() || result->empty()) {
            showPublicItemsForm(p);
            return;
        }

        std::string keyword;
        auto        keywordIt = result->find("keyword");
        if (keywordIt != result->end()) {
            if (auto* ptr = std::get_if<std::string>(&keywordIt->second)) {
                keyword = *ptr;
            }
        }
        showPublicItemsForm(p, 0, keyword);
    });
}

static void showRecycleSearchForm(Player& player);

void showPublicRecycleItemsForm(Player& player, int currentPage, const std::string& searchKeyword) {
    auto& i18n = I18nService::getInstance();

    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get("public_items.recycle_list_title"));

    auto allItems = ShopRepository::getInstance().findAllPublicRecycleItems();

    std::vector<PublicRecycleItemData> filteredItems;
    for (const auto& item : allItems) {
        if (!searchKeyword.empty()) {
            auto itemPtr = CT::FormUtils::createItemStackFromNbtString(item.itemNbt);
            if (!itemPtr) continue;
            std::string itemName = itemPtr->getName();
            std::string typeName = itemPtr->getTypeName();
            if (!fuzzyMatch(itemName, searchKeyword) && !fuzzyMatch(typeName, searchKeyword)) {
                continue;
            }
        }
        filteredItems.push_back(item);
    }

    int totalItems = static_cast<int>(filteredItems.size());
    int totalPages = (totalItems + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;
    currentPage = std::max(0, std::min(currentPage, totalPages - 1));
    fm.appendButton(i18n.get("public_shop.button_search"), "textures/ui/magnifyingGlass", "path", [](Player& p) {
        showRecycleSearchForm(p);
    });
    fm.appendButton(
        i18n.get("form.button_trade_records"),
        "textures/ui/book_edit_default",
        "path",
        [currentPage, searchKeyword](Player& p) {
            showTradeRecordMenuForm(p, [currentPage, searchKeyword](Player& playerToBack) {
                showPublicRecycleItemsForm(playerToBack, currentPage, searchKeyword);
            }, false);
        }
    );

    if (filteredItems.empty()) {
        fm.setContent(
            searchKeyword.empty() ? i18n.get("public_items.no_recycle_items") : i18n.get("public_items.no_match")
        );
    } else {
        std::string contentText = i18n.get(
            "public_items.total_items",
            {
                {"count", std::to_string(totalItems)     },
                {"page",  std::to_string(currentPage + 1)},
                {"total", std::to_string(totalPages)     }
        }
        );
        if (!searchKeyword.empty()) {
            contentText += i18n.get(
                "public_items.search_result",
                {
                    {"keyword", searchKeyword}
            }
            );
        }
        fm.setContent(contentText);

        int startIdx = currentPage * ITEMS_PER_PAGE;
        int endIdx   = std::min(startIdx + ITEMS_PER_PAGE, totalItems);

        std::vector<std::string> uuids;
        for (int i = startIdx; i < endIdx; ++i) {
            uuids.push_back(filteredItems[i].ownerUuid);
        }
        auto ownerNameCache = CT::FormUtils::getPlayerNameCache(uuids);

        for (int i = startIdx; i < endIdx; ++i) {
            const auto& item    = filteredItems[i];
            auto        itemPtr = CT::FormUtils::createItemStackFromNbtString(item.itemNbt);
            if (!itemPtr) continue;

            const std::string& ownerName       = ownerNameCache[item.ownerUuid];
            std::string        shopDisplayName = item.shopName.empty() ? i18n.get(
                                                                      "public_shop.owner_recycle_shop",
                                                                      {
                                                                          {"owner", ownerName}
            }
                                                                  )
                                                                       : item.shopName;

            std::string officialTag = item.isOfficial ? i18n.get("public_shop.official_tag") + " " : "";

            std::string buttonText = officialTag + "§b" + std::string(itemPtr->getName()) + "§r §6["
                                   + CT::MoneyFormat::format(item.price) + "]§r" + "\n§f" + shopDisplayName;

            std::string texturePath = CT::FormUtils::getItemTexturePath(*itemPtr);

            if (!texturePath.empty()) {
                fm.appendButton(buttonText, texturePath, "path", [item](Player& p) {
                    showRecycleItemDetailForm(p, item);
                });
            } else {
                fm.appendButton(buttonText, [item](Player& p) { showRecycleItemDetailForm(p, item); });
            }
        }
    }

    if (totalPages > 1) {
        if (currentPage > 0) {
            fm.appendButton(
                i18n.get("public_shop.button_prev_page"),
                "textures/ui/arrow_left",
                "path",
                [currentPage, searchKeyword](Player& p) {
                    showPublicRecycleItemsForm(p, currentPage - 1, searchKeyword);
                }
            );
        }
        if (currentPage < totalPages - 1) {
            fm.appendButton(
                i18n.get("public_shop.button_next_page"),
                "textures/ui/arrow_right",
                "path",
                [currentPage, searchKeyword](Player& p) {
                    showPublicRecycleItemsForm(p, currentPage + 1, searchKeyword);
                }
            );
        }
    }

    fm.appendButton(i18n.get("public_shop.button_close"), "textures/ui/cancel", "path", [](Player& p) {});
    fm.sendTo(player);
}

static void showRecycleSearchForm(Player& player) {
    auto&                i18n = I18nService::getInstance();
    ll::form::CustomForm fm;
    fm.setTitle(i18n.get("public_items.recycle_search_title"));
    fm.appendInput("keyword", i18n.get("public_items.search_keyword"), i18n.get("public_items.search_hint"));

    fm.sendTo(player, [](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result.has_value() || result->empty()) {
            showPublicRecycleItemsForm(p);
            return;
        }

        std::string keyword;
        auto        keywordIt = result->find("keyword");
        if (keywordIt != result->end()) {
            if (auto* ptr = std::get_if<std::string>(&keywordIt->second)) {
                keyword = *ptr;
            }
        }
        showPublicRecycleItemsForm(p, 0, keyword);
    });
}

static void showItemDetailFormImpl(
    Player&            player,
    const std::string& itemNbt,
    const std::string& ownerUuid,
    const std::string& shopName,
    double             price,
    BlockPos           pos,
    int                dimId,
    bool               isOfficial,
    bool               isRecycle
) {
    auto& i18n = I18nService::getInstance();

    auto itemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbt);
    if (!itemPtr) return;

    auto        ownerNameCache  = CT::FormUtils::getPlayerNameCache({ownerUuid});
    std::string ownerName       = ownerNameCache[ownerUuid];
    std::string ownerShopKey    = isRecycle ? "public_shop.owner_recycle_shop" : "public_shop.owner_shop";
    std::string shopDisplayName = shopName.empty() ? i18n.get(
                                                         ownerShopKey,
                                                         {
                                                             {"owner", ownerName}
    }
                                                     )
                                                   : shopName;

    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get(isRecycle ? "public_items.recycle_detail_title" : "public_items.item_detail_title"));

    // 使用 getItemDisplayString 显示完整物品信息
    std::string content  = CT::FormUtils::getItemDisplayString(*itemPtr, 0, true) + "\n\n";
    content             += i18n.get(
        isRecycle ? "public_items.recycle_price" : "public_items.item_price",
        {
            {"price", CT::MoneyFormat::format(price)}
    }
    );
    content += i18n.get(
        "public_items.item_shop",
        {
            {"shop", shopDisplayName}
    }
    );
    content += i18n.get(
        "public_items.item_location",
        {
            {"dim", CT::FormUtils::dimIdToString(dimId)},
            {"x",   std::to_string(pos.x)              },
            {"y",   std::to_string(pos.y)              },
            {"z",   std::to_string(pos.z)              }
    }
    );
    if (isOfficial) {
        content += i18n.get("public_items.item_official");
    }
    content += "\n" + i18n.get(isRecycle ? "public_shop.preview_recycle_notice" : "public_shop.preview_notice");
    fm.setContent(content);

    std::string tpHintKey = isRecycle ? "public_shop.teleport_recycle_hint" : "public_shop.teleport_hint";
    fm.appendButton(
        i18n.get("public_shop.button_teleport"),
        "textures/ui/flyingascend_pressed",
        "path",
        [pos, dimId, tpHintKey](Player& p) {
            if (CT::FormUtils::teleportToShop(p, pos, dimId)) {
                p.sendMessage(I18nService::getInstance().get(tpHintKey));
            }
        }
    );
    fm.appendButton(i18n.get("public_shop.button_back_list"), "textures/ui/arrow_left", "path", [isRecycle](Player& p) {
        isRecycle ? showPublicRecycleItemsForm(p) : showPublicItemsForm(p);
    });
    fm.appendButton(i18n.get("public_shop.button_close"), "textures/ui/cancel", "path", [](Player&) {});
    fm.sendTo(player);
}

static void showShopItemDetailForm(Player& player, const PublicShopItemData& item) {
    showItemDetailFormImpl(
        player,
        item.itemNbt,
        item.ownerUuid,
        item.shopName,
        item.price,
        item.pos,
        item.dimId,
        item.isOfficial,
        false
    );
}

static void showRecycleItemDetailForm(Player& player, const PublicRecycleItemData& item) {
    showItemDetailFormImpl(
        player,
        item.itemNbt,
        item.ownerUuid,
        item.shopName,
        item.price,
        item.pos,
        item.dimId,
        item.isOfficial,
        true
    );
}

} // namespace CT
