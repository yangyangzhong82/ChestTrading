#include "PublicItemsForm.h"
#include "FormUtils.h"
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
                                   + CT::MoneyFormat::format(item.price) + "]§r" + "\n§7" + shopDisplayName + " §e["
                                   + CT::FormUtils::dimIdToString(item.dimId) + " " + std::to_string(item.pos.x) + ","
                                   + std::to_string(item.pos.y) + "," + std::to_string(item.pos.z) + "]";

            std::string texturePath = CT::FormUtils::getItemTexturePath(*itemPtr);

            if (!texturePath.empty()) {
                fm.appendButton(buttonText, texturePath, "path", [item](Player& p) {
                    auto& i18n = I18nService::getInstance();
                    if (CT::FormUtils::teleportToShop(p, item.pos, item.dimId)) {
                        p.sendMessage(i18n.get("public_shop.teleport_hint"));
                    }
                });
            } else {
                fm.appendButton(buttonText, [item](Player& p) {
                    auto& i18n = I18nService::getInstance();
                    if (CT::FormUtils::teleportToShop(p, item.pos, item.dimId)) {
                        p.sendMessage(i18n.get("public_shop.teleport_hint"));
                    }
                });
            }
        }
    }

    // 搜索按钮
    fm.appendButton(i18n.get("public_shop.button_search"), [](Player& p) { showSearchForm(p); });

    // 分页按钮
    if (totalPages > 1) {
        if (currentPage > 0) {
            fm.appendButton(i18n.get("public_shop.button_prev_page"), [currentPage, searchKeyword](Player& p) {
                showPublicItemsForm(p, currentPage - 1, searchKeyword);
            });
        }
        if (currentPage < totalPages - 1) {
            fm.appendButton(i18n.get("public_shop.button_next_page"), [currentPage, searchKeyword](Player& p) {
                showPublicItemsForm(p, currentPage + 1, searchKeyword);
            });
        }
    }

    fm.appendButton(i18n.get("public_shop.button_close"), [](Player& p) {});
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
                                   + CT::MoneyFormat::format(item.price) + "]§r" + "\n§7" + shopDisplayName + " §e["
                                   + CT::FormUtils::dimIdToString(item.dimId) + " " + std::to_string(item.pos.x) + ","
                                   + std::to_string(item.pos.y) + "," + std::to_string(item.pos.z) + "]";

            std::string texturePath = CT::FormUtils::getItemTexturePath(*itemPtr);

            if (!texturePath.empty()) {
                fm.appendButton(buttonText, texturePath, "path", [item](Player& p) {
                    auto& i18n = I18nService::getInstance();
                    if (CT::FormUtils::teleportToShop(p, item.pos, item.dimId)) {
                        p.sendMessage(i18n.get("public_shop.teleport_recycle_hint"));
                    }
                });
            } else {
                fm.appendButton(buttonText, [item](Player& p) {
                    auto& i18n = I18nService::getInstance();
                    if (CT::FormUtils::teleportToShop(p, item.pos, item.dimId)) {
                        p.sendMessage(i18n.get("public_shop.teleport_recycle_hint"));
                    }
                });
            }
        }
    }

    fm.appendButton(i18n.get("public_shop.button_search"), [](Player& p) { showRecycleSearchForm(p); });

    if (totalPages > 1) {
        if (currentPage > 0) {
            fm.appendButton(i18n.get("public_shop.button_prev_page"), [currentPage, searchKeyword](Player& p) {
                showPublicRecycleItemsForm(p, currentPage - 1, searchKeyword);
            });
        }
        if (currentPage < totalPages - 1) {
            fm.appendButton(i18n.get("public_shop.button_next_page"), [currentPage, searchKeyword](Player& p) {
                showPublicRecycleItemsForm(p, currentPage + 1, searchKeyword);
            });
        }
    }

    fm.appendButton(i18n.get("public_shop.button_close"), [](Player& p) {});
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

} // namespace CT
