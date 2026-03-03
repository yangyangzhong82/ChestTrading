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
    if (shop.type == ChestType::Shop || shop.type == ChestType::AdminShop) {
        auto items = ShopRepository::getInstance().findAllItems(shop.pos, shop.dimId);
        logger.debug("shopContainsItem: 查询到 {} 个物品", items.size());
        for (const auto& item : items) {
            if (item.dbCount <= 0) continue;
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
    } else if (shop.type == ChestType::RecycleShop || shop.type == ChestType::AdminRecycle) {
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
static void showSearchForm(Player& player, bool isRecycle, OfficialFilter currentFilter);

using ShopPreviewCallback = void (*)(Player&, const ChestData&);
using ShopListCallback    = void (*)(Player&, int, const std::string&, const std::string&, OfficialFilter);

// 判断是否为官方商店类型
static bool isOfficialShopType(ChestType type) {
    return type == ChestType::AdminShop || type == ChestType::AdminRecycle;
}

// 判断类型是否匹配目标（考虑官方商店）
static bool matchesTargetType(ChestType chestType, ChestType targetType) {
    if (targetType == ChestType::Shop) {
        return chestType == ChestType::Shop || chestType == ChestType::AdminShop;
    } else if (targetType == ChestType::RecycleShop) {
        return chestType == ChestType::RecycleShop || chestType == ChestType::AdminRecycle;
    }
    return chestType == targetType;
}

static void showShopListFormImpl(
    Player&                 player,
    int                     currentPage,
    const std::string&      searchKeyword,
    const std::string&      searchType,
    OfficialFilter          officialFilter,
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
        if (matchesTargetType(chest.type, targetType) && chest.isPublic) {
            filterUuids.push_back(chest.ownerUuid);
        }
    }
    auto filterNameCache = CT::FormUtils::getPlayerNameCache(filterUuids);

    std::vector<ChestData> shops;
    for (const auto& chest : allChests) {
        if (matchesTargetType(chest.type, targetType) && chest.isPublic) {
            // 官方商店筛选
            bool isOfficial = isOfficialShopType(chest.type);
            if (officialFilter == OfficialFilter::Official && !isOfficial) continue;
            if (officialFilter == OfficialFilter::Player && isOfficial) continue;

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

    // 获取销量数据并按销量排序（销量高的靠前）
    auto                       salesRanking = ShopRepository::getInstance().getChestSalesRanking(1000);
    std::map<std::string, int> salesMap; // key: "dimId|x|y|z", value: totalSalesCount
    for (const auto& sale : salesRanking) {
        std::string key = std::to_string(sale.dimId) + "|" + std::to_string(sale.pos.x) + "|"
                        + std::to_string(sale.pos.y) + "|" + std::to_string(sale.pos.z);
        salesMap[key] = sale.totalSalesCount;
    }

    std::sort(shops.begin(), shops.end(), [&salesMap](const ChestData& a, const ChestData& b) {
        std::string keyA = std::to_string(a.dimId) + "|" + std::to_string(a.pos.x) + "|" + std::to_string(a.pos.y) + "|"
                         + std::to_string(a.pos.z);
        std::string keyB = std::to_string(b.dimId) + "|" + std::to_string(b.pos.x) + "|" + std::to_string(b.pos.y) + "|"
                         + std::to_string(b.pos.z);
        int salesA = salesMap.count(keyA) ? salesMap[keyA] : 0;
        int salesB = salesMap.count(keyB) ? salesMap[keyB] : 0;
        return salesA > salesB; // 降序排列
    });

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
        // 显示官方商店筛选状态
        if (officialFilter == OfficialFilter::Official) {
            contentText += "\n" + i18n.get("public_shop.filter_official_only");
        } else if (officialFilter == OfficialFilter::Player) {
            contentText += "\n" + i18n.get("public_shop.filter_player_only");
        }
        fm.setContent(contentText);
    }

    // 搜索按钮置顶
    bool isRecycle = (targetType == ChestType::RecycleShop);
    fm.appendButton(
        i18n.get("public_shop.button_search"),
        "textures/ui/magnifyingGlass",
        "path",
        [isRecycle, officialFilter](Player& p) { showSearchForm(p, isRecycle, officialFilter); }
    );

    if (!shops.empty()) {
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

            std::string shopDisplayName = shop.shopName.empty() ? i18n.get(
                                                                      keys.ownerShop,
                                                                      {
                                                                          {"owner", ownerName}
            }
                                                                  )
                                                                : shop.shopName;

            // 官方商店添加标识
            std::string officialTag;
            if (isOfficialShopType(shop.type)) {
                officialTag = i18n.get("public_shop.official_tag") + " ";
            }

            // 获取销量
            std::string salesKey = std::to_string(shop.dimId) + "|" + std::to_string(shop.pos.x) + "|"
                                 + std::to_string(shop.pos.y) + "|" + std::to_string(shop.pos.z);
            int sales = salesMap.count(salesKey) ? salesMap[salesKey] : 0;

            std::string buttonText = officialTag + "§b" + shopDisplayName + "§r\n"
                                   + i18n.get("public_shop.sales_count", {{"count", std::to_string(sales)}});

            fm.appendButton(buttonText, [shop, previewCallback](Player& p) { previewCallback(p, shop); });
        }
    }

    if (totalPages > 1) {
        if (currentPage > 0) {
            fm.appendButton(
                i18n.get("public_shop.button_prev_page"),
                "textures/ui/arrow_left",
                "path",
                [currentPage, searchKeyword, searchType, officialFilter, listCallback](Player& p) {
                    listCallback(p, currentPage - 1, searchKeyword, searchType, officialFilter);
                }
            );
        }
        if (currentPage < totalPages - 1) {
            fm.appendButton(
                i18n.get("public_shop.button_next_page"),
                "textures/ui/arrow_right",
                "path",
                [currentPage, searchKeyword, searchType, officialFilter, listCallback](Player& p) {
                    listCallback(p, currentPage + 1, searchKeyword, searchType, officialFilter);
                }
            );
        }
    }

    fm.appendButton(i18n.get("form.button_back"), "textures/ui/arrow_left", "path", [](Player& p) {});
    fm.sendTo(player);
}

static void showSearchForm(Player& player, bool isRecycle, OfficialFilter currentFilter) {
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

    // 添加官方商店筛选选项
    fm.appendDropdown(
        "official",
        i18n.get("public_shop.filter_official"),
        {i18n.get("public_shop.filter_all"),
         i18n.get("public_shop.filter_official_shops"),
         i18n.get("public_shop.filter_player_shops")},
        static_cast<int>(currentFilter)
    );

    fm.sendTo(player, [isRecycle](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result.has_value() || result->empty()) return;

        uint64         typeIdx = 0;
        std::string    keyword;
        OfficialFilter officialFilter = OfficialFilter::All;

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

        // 获取官方商店筛选选项
        auto officialIt = result->find("official");
        if (officialIt != result->end()) {
            uint64 officialIdx = 0;
            if (auto* ptr = std::get_if<uint64>(&officialIt->second)) {
                officialIdx = *ptr;
            } else if (auto* dptr = std::get_if<double>(&officialIt->second)) {
                officialIdx = static_cast<uint64>(*dptr);
            }
            officialFilter = static_cast<OfficialFilter>(officialIdx);
        }

        std::string searchType = (typeIdx == 0) ? "owner" : "item";
        logger.debug(
            "showSearchForm: 搜索类型={}, 关键词={}, 官方筛选={}",
            searchType,
            keyword,
            static_cast<int>(officialFilter)
        );
        if (isRecycle) {
            showPublicRecycleShopListForm(p, 0, keyword, searchType, officialFilter);
        } else {
            showPublicShopListForm(p, 0, keyword, searchType, officialFilter);
        }
    });
}

void showPublicShopListForm(
    Player&            player,
    int                currentPage,
    const std::string& searchKeyword,
    const std::string& searchType,
    OfficialFilter     officialFilter
) {
    showShopListFormImpl(
        player,
        currentPage,
        searchKeyword,
        searchType,
        officialFilter,
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
    const std::string& searchType,
    OfficialFilter     officialFilter
) {
    showShopListFormImpl(
        player,
        currentPage,
        searchKeyword,
        searchType,
        officialFilter,
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
        bool hasDisplayItems = false;
        for (const auto& item : items) {
            if (item.dbCount > 0) {
                hasDisplayItems = true;
                break;
            }
        }
        if (!hasDisplayItems) {
            content += i18n.get("public_shop.preview_no_items");
        } else {
            content += i18n.get("public_shop.preview_items_title");
        }
        for (const auto& item : items) {
            if (item.dbCount <= 0) continue;
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

    fm.appendButton(
        i18n.get("public_shop.button_teleport"),
        "textures/ui/flyingascend_pressed",
        "path",
        [shop](Player& p) {
            auto& i18n = I18nService::getInstance();
            if (CT::FormUtils::teleportToShop(p, shop.pos, shop.dimId)) {
                p.sendMessage(i18n.get("public_shop.teleport_hint"));
            }
        }
    );

    fm.appendButton(i18n.get("public_shop.button_back_list"), "textures/ui/arrow_left", "path", [](Player& p) {
        showPublicShopListForm(p);
    });

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

    fm.appendButton(
        i18n.get("public_shop.button_teleport"),
        "textures/ui/flyingascend_pressed",
        "path",
        [shop](Player& p) {
            auto& i18n = I18nService::getInstance();
            if (CT::FormUtils::teleportToShop(p, shop.pos, shop.dimId)) {
                p.sendMessage(i18n.get("public_shop.teleport_recycle_hint"));
            }
        }
    );

    fm.appendButton(i18n.get("public_shop.button_back_list"), "textures/ui/arrow_left", "path", [](Player& p) {
        showPublicRecycleShopListForm(p);
    });

    fm.sendTo(player);
}

// 店主列表的 i18n key 配置
struct PlayerListI18nKeys {
    const char* listTitle;
    const char* noPlayers;
    const char* totalPlayers;
    const char* playerButton;
    const char* searchTitle;
    const char* searchKeyword;
    const char* searchHint;
};

static const PlayerListI18nKeys PLAYER_LIST_I18N_KEYS = {
    "player_list.list_title",
    "player_list.no_players",
    "player_list.total_players",
    "player_list.player_button",
    "player_list.search_title",
    "player_list.search_keyword",
    "player_list.search_hint"
};

static const PlayerListI18nKeys RECYCLE_PLAYER_LIST_I18N_KEYS = {
    "player_list.recycle_list_title",
    "player_list.no_recycle_players",
    "player_list.total_players",
    "player_list.player_recycle_button",
    "player_list.recycle_search_title",
    "player_list.search_keyword",
    "player_list.search_hint"
};

static const int PLAYERS_PER_PAGE = 10;

// 店主搜索表单
static void showPlayerSearchForm(Player& player, bool isRecycle) {
    auto&                i18n = I18nService::getInstance();
    ll::form::CustomForm fm;

    const auto& keys = isRecycle ? RECYCLE_PLAYER_LIST_I18N_KEYS : PLAYER_LIST_I18N_KEYS;
    fm.setTitle(i18n.get(keys.searchTitle));
    fm.appendInput("keyword", i18n.get(keys.searchKeyword), i18n.get(keys.searchHint));

    fm.sendTo(player, [isRecycle](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result.has_value() || result->empty()) {
            showPlayerListForm(p, 0, isRecycle, "");
            return;
        }

        std::string keyword;
        auto keywordIt = result->find("keyword");
        if (keywordIt != result->end()) {
            if (auto* ptr = std::get_if<std::string>(&keywordIt->second)) {
                keyword = *ptr;
            }
        }

        showPlayerListForm(p, 0, isRecycle, keyword);
    });
}

// 显示店主列表
void showPlayerListForm(Player& player, int currentPage, bool isRecycle, const std::string& searchKeyword) {
    auto& i18n = I18nService::getInstance();
    const auto& keys = isRecycle ? RECYCLE_PLAYER_LIST_I18N_KEYS : PLAYER_LIST_I18N_KEYS;

    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get(keys.listTitle));

    auto allChests = ChestService::getInstance().getAllPublicChests();

    // 目标类型
    ChestType targetType = isRecycle ? ChestType::RecycleShop : ChestType::Shop;

    // 按店主统计商店数量
    std::map<std::string, int> ownerShopCount;
    for (const auto& chest : allChests) {
        if (matchesTargetType(chest.type, targetType) && chest.isPublic) {
            ownerShopCount[chest.ownerUuid]++;
        }
    }

    if (ownerShopCount.empty()) {
        fm.setContent(i18n.get(keys.noPlayers));
        fm.appendButton(i18n.get("form.button_back"), "textures/ui/arrow_left", "path", [](Player& p) {});
        fm.sendTo(player);
        return;
    }

    // 获取所有店主的名字
    std::vector<std::string> uuids;
    for (const auto& [uuid, count] : ownerShopCount) {
        uuids.push_back(uuid);
    }
    auto nameCache = CT::FormUtils::getPlayerNameCache(uuids);

    // 构建店主列表并应用搜索过滤
    struct PlayerInfo {
        std::string uuid;
        std::string name;
        int         shopCount;
    };
    std::vector<PlayerInfo> players;

    for (const auto& [uuid, count] : ownerShopCount) {
        std::string name = nameCache[uuid];
        if (name.empty()) name = i18n.get("public_shop.unknown_owner");

        // 搜索过滤
        if (!searchKeyword.empty() && !fuzzyMatch(name, searchKeyword)) {
            continue;
        }

        players.push_back({uuid, name, count});
    }

    // 按商店数量排序（降序）
    std::sort(players.begin(), players.end(), [](const PlayerInfo& a, const PlayerInfo& b) {
        return a.shopCount > b.shopCount;
    });

    int totalPlayers = static_cast<int>(players.size());
    int totalPages   = (totalPlayers + PLAYERS_PER_PAGE - 1) / PLAYERS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;
    currentPage = std::max(0, std::min(currentPage, totalPages - 1));

    if (players.empty()) {
        fm.setContent(i18n.get("public_shop.no_match"));
    } else {
        std::string contentText = i18n.get(
            keys.totalPlayers,
            {
                {"count", std::to_string(totalPlayers)    },
                {"page",  std::to_string(currentPage + 1) },
                {"total", std::to_string(totalPages)      }
            }
        );
        if (!searchKeyword.empty()) {
            contentText += "\n§6" + i18n.get("player_list.search_result", {{"keyword", searchKeyword}});
        }
        fm.setContent(contentText);
    }

    // 搜索按钮置顶
    fm.appendButton(
        i18n.get("player_list.button_search"),
        "textures/ui/magnifyingGlass",
        "path",
        [isRecycle](Player& p) { showPlayerSearchForm(p, isRecycle); }
    );

    if (!players.empty()) {
        int startIdx = currentPage * PLAYERS_PER_PAGE;
        int endIdx   = std::min(startIdx + PLAYERS_PER_PAGE, totalPlayers);

        for (int i = startIdx; i < endIdx; ++i) {
            const auto& info = players[i];
            std::string buttonText = i18n.get(
                keys.playerButton,
                {
                    {"name",  info.name                     },
                    {"count", std::to_string(info.shopCount)}
                }
            );

            fm.appendButton(buttonText, [uuid = info.uuid, isRecycle](Player& p) {
                showPlayerShopsForm(p, uuid, 0, isRecycle);
            });
        }
    }

    // 分页按钮
    if (totalPages > 1) {
        if (currentPage > 0) {
            fm.appendButton(
                i18n.get("public_shop.button_prev_page"),
                "textures/ui/arrow_left",
                "path",
                [currentPage, isRecycle, searchKeyword](Player& p) {
                    showPlayerListForm(p, currentPage - 1, isRecycle, searchKeyword);
                }
            );
        }
        if (currentPage < totalPages - 1) {
            fm.appendButton(
                i18n.get("public_shop.button_next_page"),
                "textures/ui/arrow_right",
                "path",
                [currentPage, isRecycle, searchKeyword](Player& p) {
                    showPlayerListForm(p, currentPage + 1, isRecycle, searchKeyword);
                }
            );
        }
    }

    fm.appendButton(i18n.get("form.button_back"), "textures/ui/arrow_left", "path", [](Player& p) {});
    fm.sendTo(player);
}

// 显示指定玩家的商店列表
void showPlayerShopsForm(Player& player, const std::string& ownerUuid, int currentPage, bool isRecycle) {
    auto& i18n = I18nService::getInstance();

    ll::form::SimpleForm fm;

    // 获取店主名称
    auto ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(ownerUuid));
    std::string ownerName = ownerInfo ? ownerInfo->name : i18n.get("public_shop.unknown_owner");

    fm.setTitle(i18n.get(
        isRecycle ? "player_list.player_recycle_shops_title" : "player_list.player_shops_title",
        {{"name", ownerName}}
    ));

    auto allChests = ChestService::getInstance().getAllPublicChests();

    ChestType targetType = isRecycle ? ChestType::RecycleShop : ChestType::Shop;

    // 筛选该店主的商店
    std::vector<ChestData> shops;
    for (const auto& chest : allChests) {
        if (matchesTargetType(chest.type, targetType) && chest.isPublic && chest.ownerUuid == ownerUuid) {
            shops.push_back(chest);
        }
    }

    // 获取销量数据并按销量排序
    auto salesRanking = ShopRepository::getInstance().getChestSalesRanking(1000);
    std::map<std::string, int> salesMap;
    for (const auto& sale : salesRanking) {
        std::string key = std::to_string(sale.dimId) + "|" + std::to_string(sale.pos.x) + "|"
                        + std::to_string(sale.pos.y) + "|" + std::to_string(sale.pos.z);
        salesMap[key] = sale.totalSalesCount;
    }

    std::sort(shops.begin(), shops.end(), [&salesMap](const ChestData& a, const ChestData& b) {
        std::string keyA = std::to_string(a.dimId) + "|" + std::to_string(a.pos.x) + "|"
                         + std::to_string(a.pos.y) + "|" + std::to_string(a.pos.z);
        std::string keyB = std::to_string(b.dimId) + "|" + std::to_string(b.pos.x) + "|"
                         + std::to_string(b.pos.y) + "|" + std::to_string(b.pos.z);
        int salesA = salesMap.count(keyA) ? salesMap[keyA] : 0;
        int salesB = salesMap.count(keyB) ? salesMap[keyB] : 0;
        return salesA > salesB;
    });

    int totalShops = static_cast<int>(shops.size());
    int totalPages = (totalShops + SHOPS_PER_PAGE - 1) / SHOPS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;
    currentPage = std::max(0, std::min(currentPage, totalPages - 1));

    const auto& i18nKeys = isRecycle ? RECYCLE_I18N_KEYS : SHOP_I18N_KEYS;

    if (shops.empty()) {
        fm.setContent(i18n.get(i18nKeys.noShops));
    } else {
        fm.setContent(i18n.get(
            i18nKeys.totalShops,
            {
                {"count", std::to_string(totalShops)     },
                {"page",  std::to_string(currentPage + 1)},
                {"total", std::to_string(totalPages)     }
            }
        ));
    }

    if (!shops.empty()) {
        int startIdx = currentPage * SHOPS_PER_PAGE;
        int endIdx   = std::min(startIdx + SHOPS_PER_PAGE, totalShops);

        for (int i = startIdx; i < endIdx; ++i) {
            const auto& shop = shops[i];

            std::string shopDisplayName = shop.shopName.empty()
                ? i18n.get(i18nKeys.ownerShop, {{"owner", ownerName}})
                : shop.shopName;

            std::string officialTag;
            if (isOfficialShopType(shop.type)) {
                officialTag = i18n.get("public_shop.official_tag") + " ";
            }

            // 获取销量
            std::string salesKey = std::to_string(shop.dimId) + "|" + std::to_string(shop.pos.x) + "|"
                                 + std::to_string(shop.pos.y) + "|" + std::to_string(shop.pos.z);
            int sales = salesMap.count(salesKey) ? salesMap[salesKey] : 0;

            std::string buttonText = officialTag + "§b" + shopDisplayName + "§r\n"
                                   + i18n.get("public_shop.sales_count", {{"count", std::to_string(sales)}});

            if (isRecycle) {
                fm.appendButton(buttonText, [shop](Player& p) { showRecycleShopPreviewForm(p, shop); });
            } else {
                fm.appendButton(buttonText, [shop](Player& p) { showShopPreviewForm(p, shop); });
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
                [ownerUuid, currentPage, isRecycle](Player& p) {
                    showPlayerShopsForm(p, ownerUuid, currentPage - 1, isRecycle);
                }
            );
        }
        if (currentPage < totalPages - 1) {
            fm.appendButton(
                i18n.get("public_shop.button_next_page"),
                "textures/ui/arrow_right",
                "path",
                [ownerUuid, currentPage, isRecycle](Player& p) {
                    showPlayerShopsForm(p, ownerUuid, currentPage + 1, isRecycle);
                }
            );
        }
    }

    // 返回店主列表
    fm.appendButton(
        i18n.get("player_list.button_back_list"),
        "textures/ui/arrow_left",
        "path",
        [isRecycle](Player& p) { showPlayerListForm(p, 0, isRecycle, ""); }
    );

    fm.sendTo(player);
}

} // namespace CT
