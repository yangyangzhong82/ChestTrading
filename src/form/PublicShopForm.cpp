#include "PublicShopForm.h"
#include "Config/ConfigManager.h"
#include "FloatingText/FloatingText.h"
#include "FormUtils.h"
#include "RecycleForm.h"
#include "ShopForm.h"
#include "Utils/MoneyFormat.h"
#include "Utils/NbtUtils.h"
#include "Utils/economy.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/PlayerInfo.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/actor/ChestBlockActor.h"
#include "repository/ChestRepository.h"
#include "repository/ItemRepository.h"
#include "repository/ShopRepository.h"
#include "service/ChestService.h"
#include "service/I18nService.h"
#include "service/TeleportService.h"
#include "service/TextService.h"
#include <algorithm>
#include <cctype>


namespace CT {

static const int SHOPS_PER_PAGE = 10;

static std::string dimIdToString(int dimId) {
    auto& i18n = I18nService::getInstance();
    switch (dimId) {
    case 0:
        return i18n.get("dimension.overworld");
    case 1:
        return i18n.get("dimension.nether");
    case 2:
        return i18n.get("dimension.end");
    default:
        return i18n.get("dimension.unknown");
    }
}

// 转换为小写用于模糊搜索
static std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

// 模糊匹配
static bool fuzzyMatch(const std::string& text, const std::string& keyword) {
    return toLower(text).find(toLower(keyword)) != std::string::npos;
}

// 检查商店是否包含匹配的物品（从Repository查询）
static bool shopContainsItem(const ChestData& shop, const std::string& keyword) {
    logger.debug(
        "shopContainsItem: 搜索商店 ({},{},{}) dim={} 关键词: {}",
        shop.pos.x,
        shop.pos.y,
        shop.pos.z,
        shop.dimId,
        keyword
    );

    std::vector<int> itemIds;
    if (shop.type == ChestType::Shop) {
        auto items = ShopRepository::getInstance().findAllItems(shop.pos, shop.dimId);
        for (const auto& item : items) {
            itemIds.push_back(item.itemId);
        }
    } else if (shop.type == ChestType::RecycleShop) {
        auto items = ShopRepository::getInstance().findAllRecycleItems(shop.pos, shop.dimId);
        for (const auto& item : items) {
            itemIds.push_back(item.itemId);
        }
    }

    logger.debug("shopContainsItem: 查询到 {} 个物品", itemIds.size());

    for (int itemId : itemIds) {
        std::string itemNbt = ItemRepository::getInstance().getItemNbt(itemId);
        if (itemNbt.empty()) continue;

        auto nbt = CT::NbtUtils::parseSNBT(itemNbt);
        if (nbt) {
            (*nbt)["Count"] = ByteTag(1);
            auto itemPtr    = CT::NbtUtils::createItemFromNbt(*nbt);
            if (itemPtr && !itemPtr->isNull()) {
                std::string itemName = itemPtr->getName();
                std::string typeName = itemPtr->getTypeName();
                if (itemName.empty()) itemName = typeName;
                if (fuzzyMatch(itemName, keyword) || fuzzyMatch(typeName, keyword)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static void showSearchForm(Player& player, bool isRecycle) {
    auto&                i18n = I18nService::getInstance();
    ll::form::CustomForm fm;
    fm.setTitle(isRecycle ? i18n.get("public_shop.search_recycle_title") : i18n.get("public_shop.search_title"));
    fm.appendDropdown(
        "type",
        i18n.get("public_shop.search_type"),
        {i18n.get("public_shop.search_by_owner"), i18n.get("public_shop.search_by_item")},
        0
    );
    fm.appendInput("keyword", i18n.get("public_shop.search_keyword"), i18n.get("public_shop.search_keyword_hint"));
    fm.sendTo(player, [isRecycle](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result.has_value() || result->empty()) return;

        uint64      typeIdx = 0;
        std::string keyword;

        auto typeIt = result->find("type");
        if (typeIt != result->end()) {
            if (auto* ptr = std::get_if<uint64>(&typeIt->second)) {
                typeIdx = *ptr;
            } else if (auto* dptr = std::get_if<double>(&typeIt->second)) {
                typeIdx = static_cast<uint64>(*dptr);
            } else if (auto* sptr = std::get_if<std::string>(&typeIt->second)) {
                // dropdown 返回字符串时，检查是否是"物品名称/类型"
                auto& i18n = I18nService::getInstance();
                if (*sptr == i18n.get("public_shop.search_by_item")) {
                    typeIdx = 1;
                }
                logger.debug("showSearchForm: type 是 string, 值={}", *sptr);
            }
        }

        auto keywordIt = result->find("keyword");
        if (keywordIt != result->end()) {
            logger.debug("showSearchForm: 找到 keyword 字段, variant index={}", keywordIt->second.index());
            if (auto* ptr = std::get_if<std::string>(&keywordIt->second)) {
                keyword = *ptr;
                logger.debug("showSearchForm: keyword={}", keyword);
            }
        } else {
            logger.warn("showSearchForm: 未找到 keyword 字段");
        }

        std::string searchType = (typeIdx == 0) ? "owner" : "item";
        logger.debug("showSearchForm: 搜索类型={}, 关键词={}", searchType, keyword);
        if (isRecycle) {
            showPublicRecycleShopListForm(p, 0, keyword, searchType);
        } else {
            showPublicShopListForm(p, 0, keyword, searchType);
        }
    });
}

void showPublicShopListForm(
    Player&            player,
    int                currentPage,
    const std::string& searchKeyword,
    const std::string& searchType
) {
    auto& i18n = I18nService::getInstance();
    logger.debug("showPublicShopListForm: 开始搜索, 关键词={}, 类型={}", searchKeyword, searchType);

    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get("public_shop.list_title"));

    auto allChests = ChestService::getInstance().getAllPublicChests();
    logger.debug("showPublicShopListForm: 获取到 {} 个箱子", allChests.size());

    std::vector<ChestData> shops;
    for (const auto& chest : allChests) {
        // 只显示公开的商店
        if (chest.type == ChestType::Shop && chest.isPublic) {
            if (!searchKeyword.empty()) {
                if (searchType == "owner") {
                    auto ownerInfo =
                        ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(chest.ownerUuid));
                    std::string ownerName = ownerInfo ? ownerInfo->name : "";
                    logger.debug("showPublicShopListForm: 检查店主 '{}' 是否匹配 '{}'", ownerName, searchKeyword);
                    if (!fuzzyMatch(ownerName, searchKeyword)) continue;
                } else if (searchType == "item") {
                    if (!shopContainsItem(chest, searchKeyword)) continue;
                }
            }
            shops.push_back(chest);
        }
    }
    logger.debug("showPublicShopListForm: 过滤后剩余 {} 个商店", shops.size());

    int totalShops = shops.size();
    int totalPages = (totalShops + SHOPS_PER_PAGE - 1) / SHOPS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;
    currentPage = std::max(0, std::min(currentPage, totalPages - 1));

    if (shops.empty()) {
        fm.setContent(searchKeyword.empty() ? i18n.get("public_shop.no_shops") : i18n.get("public_shop.no_match"));
    } else {
        std::string contentText = i18n.get(
            "public_shop.total_shops",
            {
                {"count", std::to_string(totalShops)     },
                {"page",  std::to_string(currentPage + 1)},
                {"total", std::to_string(totalPages)     }
        }
        );
        if (!searchKeyword.empty()) {
            std::string typeStr  = (searchType == "owner") ? i18n.get("public_shop.search_type_owner")
                                                           : i18n.get("public_shop.search_type_item");
            contentText         += i18n.get(
                "public_shop.search_result",
                {
                    {"type",    typeStr      },
                    {"keyword", searchKeyword}
            }
            );
        }
        fm.setContent(contentText);

        int startIdx = currentPage * SHOPS_PER_PAGE;
        int endIdx   = std::min(startIdx + SHOPS_PER_PAGE, totalShops);

        for (int i = startIdx; i < endIdx; ++i) {
            const auto& shop = shops[i];
            auto ownerInfo   = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(shop.ownerUuid));
            std::string ownerName = ownerInfo ? ownerInfo->name : i18n.get("public_shop.unknown_owner");

            std::string shopDisplayName = shop.shopName.empty() ? i18n.get(
                                                                      "public_shop.owner_shop",
                                                                      {
                                                                          {"owner", ownerName}
            }
                                                                  )
                                                                : shop.shopName;
            std::string buttonText      = "§b" + shopDisplayName + "§r\n§7" + dimIdToString(shop.dimId) + " §e["
                                   + std::to_string(shop.pos.x) + ", " + std::to_string(shop.pos.y) + ", "
                                   + std::to_string(shop.pos.z) + "]";

            fm.appendButton(buttonText, [shop](Player& p) { showShopPreviewForm(p, shop); });
        }
    }

    fm.appendButton(i18n.get("public_shop.button_search"), [](Player& p) { showSearchForm(p, false); });

    if (totalPages > 1) {
        if (currentPage > 0) {
            fm.appendButton(
                i18n.get("public_shop.button_prev_page"),
                [currentPage, searchKeyword, searchType](Player& p) {
                    showPublicShopListForm(p, currentPage - 1, searchKeyword, searchType);
                }
            );
        }
        if (currentPage < totalPages - 1) {
            fm.appendButton(
                i18n.get("public_shop.button_next_page"),
                [currentPage, searchKeyword, searchType](Player& p) {
                    showPublicShopListForm(p, currentPage + 1, searchKeyword, searchType);
                }
            );
        }
    }

    fm.appendButton(i18n.get("public_shop.button_close"), [](Player& p) {});
    fm.sendTo(player);
}

void showPublicRecycleShopListForm(
    Player&            player,
    int                currentPage,
    const std::string& searchKeyword,
    const std::string& searchType
) {
    auto&                i18n = I18nService::getInstance();
    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get("public_shop.recycle_list_title"));

    auto                   allChests = ChestService::getInstance().getAllPublicChests();
    std::vector<ChestData> recycleShops;
    for (const auto& chest : allChests) {
        // 只显示公开的回收商店
        if (chest.type == ChestType::RecycleShop && chest.isPublic) {
            if (!searchKeyword.empty()) {
                if (searchType == "owner") {
                    auto ownerInfo =
                        ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(chest.ownerUuid));
                    std::string ownerName = ownerInfo ? ownerInfo->name : "";
                    if (!fuzzyMatch(ownerName, searchKeyword)) continue;
                } else if (searchType == "item") {
                    if (!shopContainsItem(chest, searchKeyword)) continue;
                }
            }
            recycleShops.push_back(chest);
        }
    }

    int totalShops = recycleShops.size();
    int totalPages = (totalShops + SHOPS_PER_PAGE - 1) / SHOPS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;
    currentPage = std::max(0, std::min(currentPage, totalPages - 1));

    if (recycleShops.empty()) {
        fm.setContent(
            searchKeyword.empty() ? i18n.get("public_shop.no_recycle_shops") : i18n.get("public_shop.no_match_recycle")
        );
    } else {
        std::string contentText = i18n.get(
            "public_shop.total_recycle_shops",
            {
                {"count", std::to_string(totalShops)     },
                {"page",  std::to_string(currentPage + 1)},
                {"total", std::to_string(totalPages)     }
        }
        );
        if (!searchKeyword.empty()) {
            std::string typeStr  = (searchType == "owner") ? i18n.get("public_shop.search_type_owner")
                                                           : i18n.get("public_shop.search_type_item");
            contentText         += i18n.get(
                "public_shop.search_result",
                {
                    {"type",    typeStr      },
                    {"keyword", searchKeyword}
            }
            );
        }
        fm.setContent(contentText);

        int startIdx = currentPage * SHOPS_PER_PAGE;
        int endIdx   = std::min(startIdx + SHOPS_PER_PAGE, totalShops);

        for (int i = startIdx; i < endIdx; ++i) {
            const auto& shop = recycleShops[i];
            auto ownerInfo   = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(shop.ownerUuid));
            std::string ownerName = ownerInfo ? ownerInfo->name : i18n.get("public_shop.unknown_owner");

            std::string shopDisplayName = shop.shopName.empty() ? i18n.get(
                                                                      "public_shop.owner_recycle_shop",
                                                                      {
                                                                          {"owner", ownerName}
            }
                                                                  )
                                                                : shop.shopName;
            std::string buttonText      = "§b" + shopDisplayName + "§r\n§7" + dimIdToString(shop.dimId) + " §e["
                                   + std::to_string(shop.pos.x) + ", " + std::to_string(shop.pos.y) + ", "
                                   + std::to_string(shop.pos.z) + "]";

            fm.appendButton(buttonText, [shop](Player& p) { showRecycleShopPreviewForm(p, shop); });
        }
    }

    fm.appendButton(i18n.get("public_shop.button_search"), [](Player& p) { showSearchForm(p, true); });

    if (totalPages > 1) {
        if (currentPage > 0) {
            fm.appendButton(
                i18n.get("public_shop.button_prev_page"),
                [currentPage, searchKeyword, searchType](Player& p) {
                    showPublicRecycleShopListForm(p, currentPage - 1, searchKeyword, searchType);
                }
            );
        }
        if (currentPage < totalPages - 1) {
            fm.appendButton(
                i18n.get("public_shop.button_next_page"),
                [currentPage, searchKeyword, searchType](Player& p) {
                    showPublicRecycleShopListForm(p, currentPage + 1, searchKeyword, searchType);
                }
            );
        }
    }

    fm.appendButton(i18n.get("public_shop.button_close"), [](Player& p) {});
    fm.sendTo(player);
}

// 商店预览表单（只能预览物品，不能购买，可传送到箱子位置）
void showShopPreviewForm(Player& player, const ChestData& shop) {
    auto&                i18n = I18nService::getInstance();
    ll::form::SimpleForm fm;

    auto        ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(shop.ownerUuid));
    std::string ownerName = ownerInfo ? ownerInfo->name : i18n.get("public_shop.unknown_owner");
    std::string shopDisplayName = shop.shopName.empty() ? i18n.get(
                                                              "public_shop.owner_shop",
                                                              {
                                                                  {"owner", ownerName}
    }
                                                          )
                                                        : shop.shopName;

    fm.setTitle(i18n.get(
        "public_shop.preview_title",
        {
            {"name", shopDisplayName}
    }
    ));

    auto items = ShopRepository::getInstance().findAllItems(shop.pos, shop.dimId);

    std::string content = i18n.get(
        "public_shop.preview_owner",
        {
            {"owner", ownerName}
    }
    );
    content += i18n.get(
        "public_shop.preview_location",
        {
            {"dim", dimIdToString(shop.dimId) },
            {"x",   std::to_string(shop.pos.x)},
            {"y",   std::to_string(shop.pos.y)},
            {"z",   std::to_string(shop.pos.z)}
    }
    );

    if (items.empty()) {
        content += i18n.get("public_shop.preview_no_items");
    } else {
        content += i18n.get("public_shop.preview_items_title");
        for (const auto& item : items) {
            std::string itemNbtStr = ItemRepository::getInstance().getItemNbt(item.itemId);
            auto        itemPtr    = CT::FormUtils::createItemStackFromNbtString(itemNbtStr);
            if (itemPtr) {
                content += i18n.get(
                    "public_shop.preview_item_entry",
                    {
                        {"name",  std::string(itemPtr->getName())    },
                        {"count", std::to_string(item.dbCount)       },
                        {"price", CT::MoneyFormat::format(item.price)}
                }
                );
            }
        }
    }
    content += i18n.get("public_shop.preview_notice");
    fm.setContent(content);

    fm.appendButton(i18n.get("public_shop.button_teleport"), [shop](Player& p) {
        auto& i18n = I18nService::getInstance();
        if (CT::FormUtils::teleportToShop(p, shop.pos, shop.dimId)) {
            p.sendMessage(i18n.get("public_shop.teleport_hint"));
        }
    });

    fm.appendButton(i18n.get("public_shop.button_back_list"), [](Player& p) { showPublicShopListForm(p); });

    fm.appendButton(i18n.get("public_shop.button_close"), [](Player& p) {});
    fm.sendTo(player);
}

// 回收商店预览表单（只能预览物品，不能回收，可传送到箱子位置）
void showRecycleShopPreviewForm(Player& player, const ChestData& shop) {
    auto&                i18n = I18nService::getInstance();
    ll::form::SimpleForm fm;

    auto        ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(shop.ownerUuid));
    std::string ownerName = ownerInfo ? ownerInfo->name : i18n.get("public_shop.unknown_owner");
    std::string shopDisplayName = shop.shopName.empty() ? i18n.get(
                                                              "public_shop.owner_recycle_shop",
                                                              {
                                                                  {"owner", ownerName}
    }
                                                          )
                                                        : shop.shopName;

    fm.setTitle(i18n.get(
        "public_shop.recycle_preview_title",
        {
            {"name", shopDisplayName}
    }
    ));

    auto items = ShopRepository::getInstance().findAllRecycleItems(shop.pos, shop.dimId);

    std::string content = i18n.get(
        "public_shop.preview_owner",
        {
            {"owner", ownerName}
    }
    );
    content += i18n.get(
        "public_shop.preview_location",
        {
            {"dim", dimIdToString(shop.dimId) },
            {"x",   std::to_string(shop.pos.x)},
            {"y",   std::to_string(shop.pos.y)},
            {"z",   std::to_string(shop.pos.z)}
    }
    );

    if (items.empty()) {
        content += i18n.get("public_shop.preview_no_recycle");
    } else {
        content += i18n.get("public_shop.preview_recycle_title");
        for (const auto& item : items) {
            std::string itemNbtStr = ItemRepository::getInstance().getItemNbt(item.itemId);
            auto        itemPtr    = CT::FormUtils::createItemStackFromNbtString(itemNbtStr);
            if (itemPtr) {
                content += i18n.get(
                    "public_shop.preview_recycle_entry",
                    {
                        {"name",  std::string(itemPtr->getName())    },
                        {"price", CT::MoneyFormat::format(item.price)}
                }
                );
            }
        }
    }
    content += i18n.get("public_shop.preview_recycle_notice");
    fm.setContent(content);

    fm.appendButton(i18n.get("public_shop.button_teleport"), [shop](Player& p) {
        auto& i18n = I18nService::getInstance();
        if (CT::FormUtils::teleportToShop(p, shop.pos, shop.dimId)) {
            p.sendMessage(i18n.get("public_shop.teleport_recycle_hint"));
        }
    });

    fm.appendButton(i18n.get("public_shop.button_back_list"), [](Player& p) { showPublicRecycleShopListForm(p); });

    fm.appendButton(i18n.get("public_shop.button_close"), [](Player& p) {});
    fm.sendTo(player);
}

} // namespace CT
