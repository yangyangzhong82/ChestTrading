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
#include "repository/ShopRepository.h"
#include "service/ChestService.h"
#include "service/I18nService.h"
#include "service/TeleportService.h"
#include "service/TextService.h"
#include <algorithm>
#include <cctype>
#include <map>


namespace CT {

static const int SHOPS_PER_PAGE = 10;

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

// 检查商店是否包含匹配的物品（从Repository查询，已优化避免N+1）
static bool shopContainsItem(const ChestData& shop, const std::string& keyword) {
    logger.debug(
        "shopContainsItem: 搜索商店 ({},{},{}) dim={} 关键词: {}",
        shop.pos.x,
        shop.pos.y,
        shop.pos.z,
        shop.dimId,
        keyword
    );

    // findAllItems/findAllRecycleItems 已经 JOIN 了 item_definitions，直接使用 itemNbt 字段
    if (shop.type == ChestType::Shop) {
        auto items = ShopRepository::getInstance().findAllItems(shop.pos, shop.dimId);
        logger.debug("shopContainsItem: 查询到 {} 个物品", items.size());
        for (const auto& item : items) {
            if (item.itemNbt.empty()) continue;
            auto nbt = CT::NbtUtils::parseSNBT(item.itemNbt);
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
    } else if (shop.type == ChestType::RecycleShop) {
        auto items = ShopRepository::getInstance().findAllRecycleItems(shop.pos, shop.dimId);
        logger.debug("shopContainsItem: 查询到 {} 个物品", items.size());
        for (const auto& item : items) {
            if (item.itemNbt.empty()) continue;
            auto nbt = CT::NbtUtils::parseSNBT(item.itemNbt);
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
    }
    return false;
}

// 商店列表的 i18n key 配置
struct ShopListI18nKeys {
    const char* listTitle;
    const char* noShops;
    const char* noMatch;
    const char* totalShops;
    const char* ownerShop;
};

static const ShopListI18nKeys SHOP_I18N_KEYS = {
    "public_shop.list_title",
    "public_shop.no_shops",
    "public_shop.no_match",
    "public_shop.total_shops",
    "public_shop.owner_shop"
};

static const ShopListI18nKeys RECYCLE_I18N_KEYS = {
    "public_shop.recycle_list_title",
    "public_shop.no_recycle_shops",
    "public_shop.no_match_recycle",
    "public_shop.total_recycle_shops",
    "public_shop.owner_recycle_shop"
};

// 前向声明
static void showSearchForm(Player& player, bool isRecycle);

using ShopPreviewCallback = void (*)(Player&, const ChestData&);
using ShopListCallback    = void (*)(Player&, int, const std::string&, const std::string&);

static void showShopListFormImpl(
    Player&                 player,
    int                     currentPage,
    const std::string&      searchKeyword,
    const std::string&      searchType,
    ChestType               targetType,
    const ShopListI18nKeys& keys,
    ShopPreviewCallback     previewCallback,
    ShopListCallback        listCallback
) {
    auto& i18n = I18nService::getInstance();

    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get(keys.listTitle));

    auto allChests = ChestService::getInstance().getAllPublicChests();

    // 预先批量查询所有潜在店主的名称
    std::vector<std::string> filterUuids;
    for (const auto& chest : allChests) {
        if (chest.type == targetType && chest.isPublic) {
            filterUuids.push_back(chest.ownerUuid);
        }
    }
    auto filterNameCache = CT::FormUtils::getPlayerNameCache(filterUuids);

    std::vector<ChestData> shops;
    for (const auto& chest : allChests) {
        if (chest.type == targetType && chest.isPublic) {
            if (!searchKeyword.empty()) {
                if (searchType == "owner") {
                    const std::string& ownerName = filterNameCache[chest.ownerUuid];
                    if (!fuzzyMatch(ownerName, searchKeyword)) continue;
                } else if (searchType == "item") {
                    if (!shopContainsItem(chest, searchKeyword)) continue;
                }
            }
            shops.push_back(chest);
        }
    }

    int totalShops = static_cast<int>(shops.size());
    int totalPages = (totalShops + SHOPS_PER_PAGE - 1) / SHOPS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;
    currentPage = std::max(0, std::min(currentPage, totalPages - 1));

    if (shops.empty()) {
        fm.setContent(searchKeyword.empty() ? i18n.get(keys.noShops) : i18n.get(keys.noMatch));
    } else {
        std::string contentText = i18n.get(
            keys.totalShops,
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

        // 预先批量查询当前页所有玩家名称
        std::vector<std::string> uuids;
        for (int i = startIdx; i < endIdx; ++i) {
            uuids.push_back(shops[i].ownerUuid);
        }
        auto ownerNameCache = CT::FormUtils::getPlayerNameCache(uuids);

        for (int i = startIdx; i < endIdx; ++i) {
            const auto&        shop      = shops[i];
            const std::string& ownerName = ownerNameCache[shop.ownerUuid];

            std::string shopDisplayName = shop.shopName.empty() ? i18n.get(keys.ownerShop, {{"owner", ownerName}})
                                                                : shop.shopName;
            std::string buttonText = "§b" + shopDisplayName + "§r\n§7" + CT::FormUtils::dimIdToString(shop.dimId)
                                   + " §e[" + std::to_string(shop.pos.x) + ", " + std::to_string(shop.pos.y) + ", "
                                   + std::to_string(shop.pos.z) + "]";

            fm.appendButton(buttonText, [shop, previewCallback](Player& p) { previewCallback(p, shop); });
        }
    }

    bool isRecycle = (targetType == ChestType::RecycleShop);
    fm.appendButton(i18n.get("public_shop.button_search"), [isRecycle](Player& p) { showSearchForm(p, isRecycle); });

    if (totalPages > 1) {
        if (currentPage > 0) {
            fm.appendButton(
                i18n.get("public_shop.button_prev_page"),
                [currentPage, searchKeyword, searchType, listCallback](Player& p) {
                    listCallback(p, currentPage - 1, searchKeyword, searchType);
                }
            );
        }
        if (currentPage < totalPages - 1) {
            fm.appendButton(
                i18n.get("public_shop.button_next_page"),
                [currentPage, searchKeyword, searchType, listCallback](Player& p) {
                    listCallback(p, currentPage + 1, searchKeyword, searchType);
                }
            );
        }
    }

    fm.appendButton(i18n.get("public_shop.button_close"), [](Player& p) {});
    fm.sendTo(player);
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
    showShopListFormImpl(
        player,
        currentPage,
        searchKeyword,
        searchType,
        ChestType::Shop,
        SHOP_I18N_KEYS,
        showShopPreviewForm,
        showPublicShopListForm
    );
}

void showPublicRecycleShopListForm(
    Player&            player,
    int                currentPage,
    const std::string& searchKeyword,
    const std::string& searchType
) {
    showShopListFormImpl(
        player,
        currentPage,
        searchKeyword,
        searchType,
        ChestType::RecycleShop,
        RECYCLE_I18N_KEYS,
        showRecycleShopPreviewForm,
        showPublicRecycleShopListForm
    );
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
            {"dim", CT::FormUtils::dimIdToString(shop.dimId)},
            {"x",   std::to_string(shop.pos.x)              },
            {"y",   std::to_string(shop.pos.y)              },
            {"z",   std::to_string(shop.pos.z)              }
    }
    );

    if (items.empty()) {
        content += i18n.get("public_shop.preview_no_items");
    } else {
        content += i18n.get("public_shop.preview_items_title");
        for (const auto& item : items) {
            // 直接使用 findAllItems 返回的 itemNbt 字段，避免 N+1 查询
            auto itemPtr = CT::FormUtils::createItemStackFromNbtString(item.itemNbt);
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
            {"dim", CT::FormUtils::dimIdToString(shop.dimId)},
            {"x",   std::to_string(shop.pos.x)              },
            {"y",   std::to_string(shop.pos.y)              },
            {"z",   std::to_string(shop.pos.z)              }
    }
    );

    if (items.empty()) {
        content += i18n.get("public_shop.preview_no_recycle");
    } else {
        content += i18n.get("public_shop.preview_recycle_title");
        for (const auto& item : items) {
            // 直接使用 findAllRecycleItems 返回的 itemNbt 字段，避免 N+1 查询
            auto itemPtr = CT::FormUtils::createItemStackFromNbtString(item.itemNbt);
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
