#include "PublicShopForm.h"
#include "Config/ConfigManager.h"
#include "FloatingText/FloatingText.h"
#include "FormUtils.h"
#include "RecycleForm.h"
#include "ShopForm.h"
#include "Utils/MoneyFormat.h"
#include "Utils/NbtUtils.h"
#include "Utils/economy.h"
#include "db/Sqlite3Wrapper.h"
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
#include <limits>
#include <map>
#include <unordered_map>


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

static std::string buildChestSalesKey(int dimId, const BlockPos& pos);

struct PublicChestItemSummary {
    std::unordered_map<std::string, int>  activeEntryCountByChest;
    std::unordered_map<std::string, bool> keywordMatchByChest;
};

static bool itemNbtMatchesKeyword(const std::string& itemNbt, const std::string& keyword) {
    if (keyword.empty() || itemNbt.empty()) {
        return false;
    }

    auto itemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbt);
    if (!itemPtr || itemPtr->isNull()) {
        return false;
    }

    std::string itemName = itemPtr->getName();
    std::string typeName = itemPtr->getTypeName();
    if (itemName.empty()) {
        itemName = typeName;
    }
    return fuzzyMatch(itemName, keyword) || fuzzyMatch(typeName, keyword);
}

template <typename ItemData>
static void populatePublicChestItemSummary(
    const std::vector<ItemData>& items,
    const std::string&           keyword,
    PublicChestItemSummary&      summary
) {
    for (const auto& item : items) {
        std::string chestKey = buildChestSalesKey(item.dimId, item.pos);
        ++summary.activeEntryCountByChest[chestKey];

        if (!keyword.empty() && itemNbtMatchesKeyword(item.itemNbt, keyword)) {
            summary.keywordMatchByChest[chestKey] = true;
        }
    }
}

static PublicChestItemSummary buildPublicChestItemSummary(bool isRecycle, const std::string& keyword) {
    PublicChestItemSummary summary;
    if (isRecycle) {
        populatePublicChestItemSummary(ShopRepository::getInstance().findAllPublicRecycleItems(), keyword, summary);
    } else {
        populatePublicChestItemSummary(ShopRepository::getInstance().findAllPublicShopItems(), keyword, summary);
    }
    return summary;
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
static void
showSearchForm(Player& player, bool isRecycle, OfficialFilter currentFilter, PublicListSortMode currentSortMode);
static const int PLAYERS_PER_PAGE = 10;

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

static std::string buildChestSalesKey(int dimId, const BlockPos& pos) {
    return std::to_string(dimId) + "|" + std::to_string(pos.x) + "|" + std::to_string(pos.y) + "|"
         + std::to_string(pos.z);
}

static std::map<std::string, int> buildLatestChestRankMap(bool isRecycle) {
    auto& db = Sqlite3Wrapper::getInstance();
    auto  rows = db.query(
        isRecycle
            ? "SELECT dim_id, pos_x, pos_y, pos_z FROM recycle_shop_items "
              "GROUP BY dim_id, pos_x, pos_y, pos_z ORDER BY MAX(rowid) DESC;"
            : "SELECT dim_id, pos_x, pos_y, pos_z FROM shop_items "
              "GROUP BY dim_id, pos_x, pos_y, pos_z ORDER BY MAX(rowid) DESC;"
    );

    std::map<std::string, int> rankMap;
    int                        rank = 0;
    for (const auto& row : rows) {
        if (row.size() < 4) continue;
        try {
            int      dimId = std::stoi(row[0]);
            BlockPos pos{std::stoi(row[1]), std::stoi(row[2]), std::stoi(row[3])};
            rankMap.emplace(buildChestSalesKey(dimId, pos), rank++);
        } catch (const std::exception&) {}
    }
    return rankMap;
}

static std::string buildPreviewTeleportCostText() {
    double teleportCost = ConfigManager::getInstance().get().teleportSettings.teleportCost;
    if (teleportCost <= 0.0) {
        return {};
    }

    return I18nService::getInstance().get(
        "public_shop.preview_teleport_cost",
        {{"cost", CT::MoneyFormat::format(teleportCost)}}
    );
}

static std::string buildRecyclePreviewCountText(const RecycleItemData& item) {
    auto& i18n = I18nService::getInstance();
    if (item.maxRecycleCount <= 0) {
        return i18n.get("public_shop.preview_recycle_unlimited");
    }

    int remaining = std::max(0, item.maxRecycleCount - item.currentRecycledCount);
    return i18n.get("public_shop.preview_recycle_remaining", {{"count", std::to_string(remaining)}});
}

static void showShopListFormImpl(
    Player&                 player,
    int                     currentPage,
    const std::string&      searchKeyword,
    const std::string&      searchType,
    OfficialFilter          officialFilter,
    PublicListSortMode      sortMode,
    ChestType               targetType,
    const ShopListI18nKeys& keys
) {
    auto& i18n = I18nService::getInstance();

    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get(keys.listTitle));
    bool isRecycle = (targetType == ChestType::RecycleShop);
    auto chestItemSummary = buildPublicChestItemSummary(isRecycle, searchType == "item" ? searchKeyword : "");

    auto allChests = ChestService::getInstance().getAllPublicChests();

    std::vector<ChestData> filteredChests;
    for (const auto& chest : allChests) {
        if (!matchesTargetType(chest.type, targetType) || !chest.isPublic) continue;

        bool isOfficial = isOfficialShopType(chest.type);
        if (officialFilter == OfficialFilter::Official && !isOfficial) continue;
        if (officialFilter == OfficialFilter::Player && isOfficial) continue;
        filteredChests.push_back(chest);
    }

    std::map<std::string, int> activeEntryCountCache;
    if (isRecycle) {
        for (const auto& chest : filteredChests) {
            std::string chestKey = buildChestSalesKey(chest.dimId, chest.pos);
            int         activeCount =
                chestItemSummary.activeEntryCountByChest.count(chestKey)
                ? chestItemSummary.activeEntryCountByChest[chestKey]
                : 0;
            activeEntryCountCache[chestKey] = activeCount;
        }
    }

    std::map<std::string, std::vector<ChestData>> ownerShops;
    for (const auto& chest : filteredChests) {
        ownerShops[chest.ownerUuid].push_back(chest);
    }

    std::vector<std::string> ownerUuids;
    ownerUuids.reserve(ownerShops.size());
    for (const auto& [uuid, _] : ownerShops) {
        ownerUuids.push_back(uuid);
    }
    auto ownerNameCache = CT::FormUtils::getPlayerNameCache(ownerUuids);

    auto                       salesRanking = isRecycle ? ShopRepository::getInstance().getRecycleChestSalesRanking(10000)
                                                        : ShopRepository::getInstance().getChestSalesRanking(10000);
    std::map<std::string, int> salesMap;
    for (const auto& sale : salesRanking) {
        salesMap[buildChestSalesKey(sale.dimId, sale.pos)] = sale.totalSalesCount;
    }
    auto latestRankMap = buildLatestChestRankMap(isRecycle);

    struct PlayerAggregateInfo {
        std::string ownerUuid;
        std::string ownerName;
        int         shopCount;
        int         activeEntryCount;
        int         totalSalesCount;
        int         latestRank;
    };
    std::vector<PlayerAggregateInfo> players;

    for (const auto& [ownerUuid, shops] : ownerShops) {
        std::string ownerName = ownerNameCache[ownerUuid];
        if (ownerName.empty()) ownerName = i18n.get("public_shop.unknown_owner");

        if (!searchKeyword.empty()) {
            if (searchType == "owner") {
                if (!fuzzyMatch(ownerName, searchKeyword)) continue;
            } else if (searchType == "item") {
                bool itemMatched = std::any_of(shops.begin(), shops.end(), [&chestItemSummary](const ChestData& shop) {
                    std::string chestKey = buildChestSalesKey(shop.dimId, shop.pos);
                    auto        it       = chestItemSummary.keywordMatchByChest.find(chestKey);
                    return it != chestItemSummary.keywordMatchByChest.end() && it->second;
                });
                if (!itemMatched) continue;
            }
        }

        int totalSalesCount  = 0;
        int activeEntryCount = 0;
        int latestRank       = std::numeric_limits<int>::max();
        for (const auto& shop : shops) {
            std::string chestKey = buildChestSalesKey(shop.dimId, shop.pos);

            auto salesIt = salesMap.find(chestKey);
            if (salesIt != salesMap.end()) totalSalesCount += salesIt->second;

            auto activeIt = activeEntryCountCache.find(chestKey);
            if (activeIt == activeEntryCountCache.end()) {
                int activeCount =
                    chestItemSummary.activeEntryCountByChest.count(chestKey)
                    ? chestItemSummary.activeEntryCountByChest[chestKey]
                    : 0;
                activeEntryCountCache[chestKey] = activeCount;
                activeEntryCount += activeCount;
            } else {
                activeEntryCount += activeIt->second;
            }

            auto latestIt = latestRankMap.find(chestKey);
            if (latestIt != latestRankMap.end()) {
                latestRank = std::min(latestRank, latestIt->second);
            }
        }

        players.push_back(
            {ownerUuid, ownerName, static_cast<int>(shops.size()), activeEntryCount, totalSalesCount, latestRank}
        );
    }

    std::sort(players.begin(), players.end(), [sortMode](const PlayerAggregateInfo& a, const PlayerAggregateInfo& b) {
        if (sortMode == PublicListSortMode::Latest) {
            if (a.latestRank != b.latestRank) return a.latestRank < b.latestRank;
            if (a.totalSalesCount != b.totalSalesCount) return a.totalSalesCount > b.totalSalesCount;
        } else {
            if (a.totalSalesCount != b.totalSalesCount) return a.totalSalesCount > b.totalSalesCount;
            if (a.latestRank != b.latestRank) return a.latestRank < b.latestRank;
        }
        if (a.activeEntryCount != b.activeEntryCount) return a.activeEntryCount > b.activeEntryCount;
        if (a.shopCount != b.shopCount) return a.shopCount > b.shopCount;
        return a.ownerName < b.ownerName;
    });

    int totalPlayers = static_cast<int>(players.size());
    int totalPages   = (totalPlayers + PLAYERS_PER_PAGE - 1) / PLAYERS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;
    currentPage = std::max(0, std::min(currentPage, totalPages - 1));

    if (players.empty()) {
        fm.setContent(searchKeyword.empty() ? i18n.get(isRecycle ? "player_list.no_recycle_players" : "player_list.no_players")
                                            : i18n.get(keys.noMatch));
    } else {
        std::string contentText = i18n.get(
            "player_list.total_players",
            {
                {"count", std::to_string(totalPlayers)   },
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
        contentText += "\n" + i18n.get(
                           sortMode == PublicListSortMode::Latest ? "public_shop.sort_current_latest"
                                                                  : "public_shop.sort_current_sales"
                       );
        fm.setContent(contentText);
    }

    fm.appendButton(
        i18n.get("public_shop.button_search"),
        "textures/ui/magnifyingGlass",
        "path",
        [isRecycle, officialFilter, sortMode](Player& p) { showSearchForm(p, isRecycle, officialFilter, sortMode); }
    );
    fm.appendButton(i18n.get(
                        sortMode == PublicListSortMode::Latest ? "public_shop.button_sort_latest"
                                                               : "public_shop.button_sort_sales"
                    ),
                    [isRecycle, searchKeyword, searchType, officialFilter, sortMode](Player& p) {
                        PublicListSortMode nextSort =
                            (sortMode == PublicListSortMode::Latest) ? PublicListSortMode::Sales
                                                                     : PublicListSortMode::Latest;
                        if (isRecycle) {
                            showPublicRecycleShopListForm(p, 0, searchKeyword, searchType, officialFilter, nextSort);
                        } else {
                            showPublicShopListForm(p, 0, searchKeyword, searchType, officialFilter, nextSort);
                        }
                    });

    if (!players.empty()) {
        int startIdx = currentPage * PLAYERS_PER_PAGE;
        int endIdx   = std::min(startIdx + PLAYERS_PER_PAGE, totalPlayers);
        const char* buttonKey =
            isRecycle ? "player_list.player_recycle_stats_button" : "player_list.player_shop_stats_button";

        for (int i = startIdx; i < endIdx; ++i) {
            const auto& info = players[i];
            std::string buttonText = i18n.get(
                buttonKey,
                {
                    {"name",   info.ownerName                     },
                    {"active", std::to_string(info.activeEntryCount)},
                    {"sales",  std::to_string(info.totalSalesCount)},
                    {"count",  std::to_string(info.shopCount)     }
                }
            );

            fm.appendButton(
                buttonText,
                [ownerUuid = info.ownerUuid, isRecycle, officialFilter, sortMode](Player& p) {
                    showPlayerShopsForm(p, ownerUuid, 0, isRecycle, officialFilter, sortMode);
                }
            );
        }
    }

    if (totalPages > 1) {
        if (currentPage > 0) {
            fm.appendButton(
                i18n.get("public_shop.button_prev_page"),
                "textures/ui/arrow_left",
                "path",
                [currentPage, searchKeyword, searchType, officialFilter, sortMode, isRecycle](Player& p) {
                    if (isRecycle) {
                        showPublicRecycleShopListForm(
                            p,
                            currentPage - 1,
                            searchKeyword,
                            searchType,
                            officialFilter,
                            sortMode
                        );
                    } else {
                        showPublicShopListForm(p, currentPage - 1, searchKeyword, searchType, officialFilter, sortMode);
                    }
                }
            );
        }
        if (currentPage < totalPages - 1) {
            fm.appendButton(
                i18n.get("public_shop.button_next_page"),
                "textures/ui/arrow_right",
                "path",
                [currentPage, searchKeyword, searchType, officialFilter, sortMode, isRecycle](Player& p) {
                    if (isRecycle) {
                        showPublicRecycleShopListForm(
                            p,
                            currentPage + 1,
                            searchKeyword,
                            searchType,
                            officialFilter,
                            sortMode
                        );
                    } else {
                        showPublicShopListForm(p, currentPage + 1, searchKeyword, searchType, officialFilter, sortMode);
                    }
                }
            );
        }
    }

    fm.appendButton(i18n.get("form.button_back"), "textures/ui/arrow_left", "path", [](Player&) {});
    fm.sendTo(player);
}

static void showSearchForm(
    Player&            player,
    bool               isRecycle,
    OfficialFilter     currentFilter,
    PublicListSortMode currentSortMode
) {
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

    fm.sendTo(
        player,
        [isRecycle, currentSortMode](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
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
            showPublicRecycleShopListForm(p, 0, keyword, searchType, officialFilter, currentSortMode);
        } else {
            showPublicShopListForm(p, 0, keyword, searchType, officialFilter, currentSortMode);
        }
        }
    );
}

void showPublicShopListForm(
    Player&            player,
    int                currentPage,
    const std::string& searchKeyword,
    const std::string& searchType,
    OfficialFilter     officialFilter,
    PublicListSortMode sortMode
) {
    showShopListFormImpl(
        player,
        currentPage,
        searchKeyword,
        searchType,
        officialFilter,
        sortMode,
        ChestType::Shop,
        SHOP_I18N_KEYS
    );
}

void showPublicRecycleShopListForm(
    Player&            player,
    int                currentPage,
    const std::string& searchKeyword,
    const std::string& searchType,
    OfficialFilter     officialFilter,
    PublicListSortMode sortMode
) {
    showShopListFormImpl(
        player,
        currentPage,
        searchKeyword,
        searchType,
        officialFilter,
        sortMode,
        ChestType::RecycleShop,
        RECYCLE_I18N_KEYS
    );
}

// 商店预览表单（只能预览物品，不能购买，可传送到箱子位置）
void showShopPreviewForm(Player& player, const ChestData& shop, std::function<void(Player&)> onBack) {
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
    content += buildPreviewTeleportCostText();

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

    fm.appendButton(
        i18n.get("public_shop.button_back_list"),
        "textures/ui/arrow_left",
        "path",
        [onBack](Player& p) {
            if (onBack) {
                onBack(p);
            } else {
                showPublicShopListForm(p);
            }
        }
    );

    fm.sendTo(player);
}

// 回收商店预览表单（只能预览物品，不能回收，可传送到箱子位置）
void showRecycleShopPreviewForm(Player& player, const ChestData& shop, std::function<void(Player&)> onBack) {
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
    content += buildPreviewTeleportCostText();

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
                        {"name",  std::string(itemPtr->getName())        },
                        {"price", CT::MoneyFormat::format(item.price)    },
                        {"count", buildRecyclePreviewCountText(item)}
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

    fm.appendButton(
        i18n.get("public_shop.button_back_list"),
        "textures/ui/arrow_left",
        "path",
        [onBack](Player& p) {
            if (onBack) {
                onBack(p);
            } else {
                showPublicRecycleShopListForm(p);
            }
        }
    );

    fm.sendTo(player);
}

// 显示指定玩家的商店列表
void showPlayerShopsForm(
    Player&            player,
    const std::string& ownerUuid,
    int                currentPage,
    bool               isRecycle,
    OfficialFilter     officialFilter,
    PublicListSortMode sortMode
) {
    auto& i18n = I18nService::getInstance();

    ll::form::SimpleForm fm;

    // 获取店主名称
    auto ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(ownerUuid));
    std::string ownerName = ownerInfo ? ownerInfo->name : i18n.get("public_shop.unknown_owner");

    fm.setTitle(i18n.get(
        isRecycle ? "player_list.player_recycle_shops_title" : "player_list.player_shops_title",
        {{"name", ownerName}}
    ));

    auto chestItemSummary = buildPublicChestItemSummary(isRecycle, "");
    auto allChests = ChestService::getInstance().getAllPublicChests();

    ChestType targetType = isRecycle ? ChestType::RecycleShop : ChestType::Shop;

    // 筛选该店主的商店
    std::vector<ChestData> shops;
    for (const auto& chest : allChests) {
        if (!matchesTargetType(chest.type, targetType) || !chest.isPublic || chest.ownerUuid != ownerUuid) continue;

        bool isOfficial = isOfficialShopType(chest.type);
        if (officialFilter == OfficialFilter::Official && !isOfficial) continue;
        if (officialFilter == OfficialFilter::Player && isOfficial) continue;

        shops.push_back(chest);
    }

    // 获取销量数据并按销量排序
    auto salesRanking = isRecycle ? ShopRepository::getInstance().getRecycleChestSalesRanking(10000)
                                  : ShopRepository::getInstance().getChestSalesRanking(10000);
    std::map<std::string, int> salesMap;
    for (const auto& sale : salesRanking) {
        salesMap[buildChestSalesKey(sale.dimId, sale.pos)] = sale.totalSalesCount;
    }
    auto latestRankMap = buildLatestChestRankMap(isRecycle);

    std::sort(shops.begin(), shops.end(), [&salesMap, &latestRankMap, sortMode](const ChestData& a, const ChestData& b) {
        std::string keyA = buildChestSalesKey(a.dimId, a.pos);
        std::string keyB = buildChestSalesKey(b.dimId, b.pos);
        int         salesA = salesMap.count(keyA) ? salesMap[keyA] : 0;
        int         salesB = salesMap.count(keyB) ? salesMap[keyB] : 0;
        int         latestA = latestRankMap.count(keyA) ? latestRankMap[keyA] : std::numeric_limits<int>::max();
        int         latestB = latestRankMap.count(keyB) ? latestRankMap[keyB] : std::numeric_limits<int>::max();

        if (sortMode == PublicListSortMode::Latest) {
            if (latestA != latestB) return latestA < latestB;
            return salesA > salesB;
        }
        if (salesA != salesB) return salesA > salesB;
        return latestA < latestB;
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
            std::string salesKey = buildChestSalesKey(shop.dimId, shop.pos);
            int sales = salesMap.count(salesKey) ? salesMap[salesKey] : 0;
            int activeEntryCount =
                chestItemSummary.activeEntryCountByChest.count(salesKey)
                ? chestItemSummary.activeEntryCountByChest[salesKey]
                : 0;

            std::string buttonText = officialTag + "§b" + shopDisplayName + "§r\n"
                                   + i18n.get(
                                       isRecycle ? "public_shop.recycle_button_stats" : "public_shop.shop_button_stats",
                                       {
                                           {"active", std::to_string(activeEntryCount)},
                                           {"sales",  std::to_string(sales)           }
                                       }
                                   );

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
                [ownerUuid, currentPage, isRecycle, officialFilter, sortMode](Player& p) {
                    showPlayerShopsForm(p, ownerUuid, currentPage - 1, isRecycle, officialFilter, sortMode);
                }
            );
        }
        if (currentPage < totalPages - 1) {
            fm.appendButton(
                i18n.get("public_shop.button_next_page"),
                "textures/ui/arrow_right",
                "path",
                [ownerUuid, currentPage, isRecycle, officialFilter, sortMode](Player& p) {
                    showPlayerShopsForm(p, ownerUuid, currentPage + 1, isRecycle, officialFilter, sortMode);
                }
            );
        }
    }

    // 返回店主列表
    fm.appendButton(
        i18n.get("player_list.button_back_list"),
        "textures/ui/arrow_left",
        "path",
        [isRecycle, officialFilter, sortMode](Player& p) {
            if (isRecycle) {
                showPublicRecycleShopListForm(p, 0, "", "owner", officialFilter, sortMode);
            } else {
                showPublicShopListForm(p, 0, "", "owner", officialFilter, sortMode);
            }
        }
    );

    fm.sendTo(player);
}

} // namespace CT
