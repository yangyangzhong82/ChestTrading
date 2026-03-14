#include "TradeRecordForm.h"
#include "FormUtils.h"
#include "PublicShopForm.h"
#include "Utils/MoneyFormat.h"
#include "compat/PermissionCompat.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/PlayerInfo.h"
#include "mc/platform/UUID.h"
#include "mc/world/level/Level.h"
#include "repository/ShopRepository.h"
#include "service/I18nService.h"
#include <algorithm>
#include <cctype>
#include <optional>
#include <unordered_set>
#include <vector>

namespace CT {

namespace {

constexpr int RECORDS_PER_PAGE = 10;

enum class TradeRecordTypeFilter {
    All      = 0,
    Purchase = 1,
    Recycle  = 2
};

enum class TradeRecordKeywordField {
    Player = 0,
    Item   = 1,
    Shop   = 2
};

struct TradeRecordListState {
    int                      currentPage         = 0;
    TradeRecordTypeFilter    typeFilter          = TradeRecordTypeFilter::All;
    TradeRecordKeywordField  keywordField        = TradeRecordKeywordField::Item;
    OfficialFilter           officialFilter      = OfficialFilter::All;
    std::string              keyword;
    std::optional<std::string> actorUuid;
    std::string              actorName;
    std::optional<int>       dimId;
    std::optional<BlockPos>  pos;
    std::optional<int>       itemId;
    bool                     allowOfficialFilter = true;
    bool                     allowPlayerKeyword  = true;
    bool                     allowItemKeyword    = true;
    bool                     allowShopKeyword    = true;
    bool                     allowRecordTeleport = true;
    std::string              title;
    std::function<void(Player&)> onBack;
};

struct DisplayTradeRecord {
    TradeRecordData raw;
    std::string     actorName;
    std::string     ownerName;
    std::string     shopDisplayName;
    std::string     itemName;
    std::string     itemType;
    std::string     texturePath;
    double          unitPrice = 0.0;
};

struct PlayerLookupCandidate {
    std::string uuid;
    std::string name;
};

bool isAdmin(const Player& player) {
    return PermissionCompat::hasPermission(player.getUuid().asString(), "chest.admin");
}

std::string toLower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return str;
}

std::string trimCopy(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    auto end   = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

bool fuzzyMatch(const std::string& text, const std::string& keyword) {
    if (keyword.empty()) return true;
    return toLower(text).find(toLower(keyword)) != std::string::npos;
}

int getDropdownIndex(const ll::form::CustomFormResult& result, const std::string& key, int fallback) {
    if (!result.has_value()) return fallback;
    auto it = result->find(key);
    if (it == result->end()) return fallback;
    if (auto* val = std::get_if<uint64>(&it->second)) return static_cast<int>(*val);
    if (auto* val = std::get_if<double>(&it->second)) return static_cast<int>(*val);
    return fallback;
}

int getDropdownIndex(
    const ll::form::CustomFormResult& result,
    const std::string&                key,
    const std::vector<std::string>&   options,
    int                               fallback
) {
    if (!result.has_value()) return fallback;
    auto it = result->find(key);
    if (it == result->end()) return fallback;
    if (auto* val = std::get_if<uint64>(&it->second)) return static_cast<int>(*val);
    if (auto* val = std::get_if<double>(&it->second)) return static_cast<int>(*val);
    if (auto* val = std::get_if<std::string>(&it->second)) {
        auto found = std::find(options.begin(), options.end(), *val);
        if (found != options.end()) {
            return static_cast<int>(std::distance(options.begin(), found));
        }
    }
    return fallback;
}

std::string getTradeTypeLabel(TradeRecordKind kind) {
    auto& i18n = I18nService::getInstance();
    return i18n.get(kind == TradeRecordKind::Purchase ? "trade_records.type_purchase" : "trade_records.type_recycle");
}

std::string getFilterTypeLabel(TradeRecordTypeFilter filter) {
    auto& i18n = I18nService::getInstance();
    switch (filter) {
    case TradeRecordTypeFilter::Purchase:
        return i18n.get("trade_records.filter_purchase_only");
    case TradeRecordTypeFilter::Recycle:
        return i18n.get("trade_records.filter_recycle_only");
    default:
        return i18n.get("trade_records.filter_all_types");
    }
}

std::string getKeywordFieldLabel(TradeRecordKeywordField field) {
    auto& i18n = I18nService::getInstance();
    switch (field) {
    case TradeRecordKeywordField::Player:
        return i18n.get("trade_records.keyword_player");
    case TradeRecordKeywordField::Shop:
        return i18n.get("trade_records.keyword_shop");
    default:
        return i18n.get("trade_records.keyword_item");
    }
}

std::string getOfficialFilterLabel(OfficialFilter filter) {
    auto& i18n = I18nService::getInstance();
    switch (filter) {
    case OfficialFilter::Official:
        return i18n.get("trade_records.official_only");
    case OfficialFilter::Player:
        return i18n.get("trade_records.player_only");
    default:
        return i18n.get("trade_records.official_all");
    }
}

std::string getAnnouncementHeadline(const TradeRecordListState& state) {
    auto& i18n = I18nService::getInstance();
    if (state.pos && state.dimId) {
        return i18n.get("trade_records.announcement_shop");
    }
    if (!state.actorName.empty()) {
        return i18n.get("trade_records.announcement_player", {{"player", state.actorName}});
    }
    return i18n.get("trade_records.announcement_global");
}

std::string getTradeTargetName(const DisplayTradeRecord& record) {
    auto&       i18n = I18nService::getInstance();
    std::string targetName = record.ownerName;
    if (record.raw.isOfficial) {
        return i18n.get(
            record.raw.kind == TradeRecordKind::Purchase ? "trade_records.shop_target_official_purchase"
                                                         : "trade_records.shop_target_official_recycle"
        );
    }
    if (targetName.empty() || targetName == i18n.get("public_shop.unknown_owner")) {
        targetName = record.shopDisplayName;
    }
    return targetName;
}

std::vector<TradeRecordKeywordField> getAllowedKeywordFields(const TradeRecordListState& state) {
    std::vector<TradeRecordKeywordField> fields;
    if (state.allowPlayerKeyword) fields.push_back(TradeRecordKeywordField::Player);
    if (state.allowItemKeyword) fields.push_back(TradeRecordKeywordField::Item);
    if (state.allowShopKeyword) fields.push_back(TradeRecordKeywordField::Shop);
    if (fields.empty()) fields.push_back(TradeRecordKeywordField::Item);
    return fields;
}

std::vector<std::string> getKeywordFieldLabels(const std::vector<TradeRecordKeywordField>& fields) {
    std::vector<std::string> labels;
    labels.reserve(fields.size());
    for (auto field : fields) {
        labels.push_back(getKeywordFieldLabel(field));
    }
    return labels;
}

DisplayTradeRecord toDisplayTradeRecord(
    const TradeRecordData&                  record,
    const std::map<std::string, std::string>& playerNameCache
) {
    auto& i18n = I18nService::getInstance();

    DisplayTradeRecord display{record};
    auto actorIt           = playerNameCache.find(record.actorUuid);
    auto ownerIt           = playerNameCache.find(record.ownerUuid);
    display.actorName      = actorIt != playerNameCache.end() ? actorIt->second : i18n.get("public_shop.unknown_owner");
    display.ownerName      = ownerIt != playerNameCache.end() ? ownerIt->second : i18n.get("public_shop.unknown_owner");
    const char* ownerShopKey = (record.kind == TradeRecordKind::Purchase) ? "public_shop.owner_shop"
                                                                          : "public_shop.owner_recycle_shop";
    display.shopDisplayName = record.shopName.empty()
        ? i18n.get(ownerShopKey, {{"owner", display.ownerName}})
        : record.shopName;

    auto itemPtr = FormUtils::createItemStackFromNbtString(record.itemNbt);
    if (itemPtr) {
        display.itemName    = itemPtr->getName();
        display.itemType    = itemPtr->getTypeName();
        display.texturePath = FormUtils::getItemTexturePath(*itemPtr);
    } else {
        display.itemName = i18n.get("shop.unknown_item");
        display.itemType.clear();
    }
    display.unitPrice = record.tradeCount > 0 ? record.totalPrice / static_cast<double>(record.tradeCount) : record.totalPrice;
    return display;
}

std::vector<DisplayTradeRecord> resolveTradeRecords(const std::vector<TradeRecordData>& records) {
    std::vector<std::string> uuids;
    uuids.reserve(records.size() * 2);
    for (const auto& record : records) {
        uuids.push_back(record.actorUuid);
        uuids.push_back(record.ownerUuid);
    }

    auto nameCache = FormUtils::getPlayerNameCache(uuids);

    std::vector<DisplayTradeRecord> displayRecords;
    displayRecords.reserve(records.size());
    for (const auto& record : records) {
        displayRecords.push_back(toDisplayTradeRecord(record, nameCache));
    }
    return displayRecords;
}

bool matchesKeyword(const DisplayTradeRecord& record, const TradeRecordListState& state) {
    if (state.keyword.empty()) return true;

    switch (state.keywordField) {
    case TradeRecordKeywordField::Player:
        return fuzzyMatch(record.actorName, state.keyword);
    case TradeRecordKeywordField::Shop:
        return fuzzyMatch(record.shopDisplayName, state.keyword);
    case TradeRecordKeywordField::Item:
    default:
        return fuzzyMatch(record.itemName, state.keyword) || fuzzyMatch(record.itemType, state.keyword);
    }
}

std::string buildListContent(const TradeRecordListState& state, int totalRecords, int currentPage, int totalPages) {
    auto& i18n = I18nService::getInstance();

    std::string content = getAnnouncementHeadline(state);
    content += "\n" + i18n.get(
        "trade_records.page_info",
        {
            {"count", std::to_string(totalRecords)},
            {"page",  std::to_string(currentPage + 1)},
            {"total", std::to_string(totalPages)}
        }
    );

    if (!state.actorName.empty()) {
        content += "\n" + i18n.get("trade_records.scope_player", {{"player", state.actorName}});
    }
    if (state.pos && state.dimId) {
        content += "\n" + i18n.get(
            "trade_records.scope_shop",
            {
                {"dim", FormUtils::dimIdToString(*state.dimId)},
                {"x",   std::to_string(state.pos->x)},
                {"y",   std::to_string(state.pos->y)},
                {"z",   std::to_string(state.pos->z)}
            }
        );
    }

    content += "\n" + i18n.get("trade_records.filter_trade_type_label", {{"type", getFilterTypeLabel(state.typeFilter)}});
    if (state.allowOfficialFilter) {
        content += "\n" + i18n.get(
            "trade_records.filter_official_label",
            {{"type", getOfficialFilterLabel(state.officialFilter)}}
        );
    }
    if (!state.keyword.empty()) {
        content += "\n" + i18n.get(
            "trade_records.filter_keyword_label",
            {
                {"type",    getKeywordFieldLabel(state.keywordField)},
                {"keyword", state.keyword}
            }
        );
    }
    return content;
}

std::string buildAnnouncementEntry(const DisplayTradeRecord& record, int index) {
    auto&       i18n        = I18nService::getInstance();
    std::string counterText = std::to_string(index) + ".";
    auto        targetName  = getTradeTargetName(record);

    if (record.raw.kind == TradeRecordKind::Purchase) {
        return i18n.get(
            "trade_records.entry_purchase",
            {
                {"index",  counterText                              },
                {"time",   record.raw.timestamp                     },
                {"player", record.actorName                         },
                {"target", targetName                               },
                {"shop",   record.shopDisplayName                   },
                {"item",   record.itemName                          },
                {"count",  std::to_string(record.raw.tradeCount)    },
                {"unit",   MoneyFormat::format(record.unitPrice)    },
                {"price", MoneyFormat::format(record.raw.totalPrice)}
            }
        );
    }

    return i18n.get(
        "trade_records.entry_recycle",
        {
            {"index",  counterText                              },
            {"time",   record.raw.timestamp                     },
            {"player", record.actorName                         },
            {"target", targetName                               },
            {"shop",   record.shopDisplayName                   },
            {"item",   record.itemName                          },
            {"count",  std::to_string(record.raw.tradeCount)    },
            {"unit",   MoneyFormat::format(record.unitPrice)    },
            {"price", MoneyFormat::format(record.raw.totalPrice)}
        }
    );
}

void showLastPurchasePreviewForm(Player& player, std::function<void(Player&)> onBack) {
    auto& i18n = I18nService::getInstance();
    auto  lastRecord = ShopRepository::getInstance().getLatestPurchaseRecord(player.getUuid().asString());
    if (!lastRecord) {
        player.sendMessage(i18n.get("trade_records.no_last_purchase"));
        return;
    }

    auto chestInfo = ChestRepository::getInstance().findByPosition(lastRecord->pos, lastRecord->dimId);
    if (!chestInfo
        || (chestInfo->type != ChestType::Shop && chestInfo->type != ChestType::AdminShop)) {
        player.sendMessage(i18n.get("trade_records.last_purchase_shop_missing"));
        return;
    }

    showShopPreviewForm(player, *chestInfo, onBack);
}

void showTradeRecordListForm(Player& player, TradeRecordListState state);
void showPlayerLookupForm(Player& player, std::function<void(Player&)> onBack, const std::string& initialKeyword = {});

void openPlayerTradeRecords(Player& player, const PlayerLookupCandidate& candidate, std::function<void(Player&)> onBack) {
    auto& i18n = I18nService::getInstance();

    TradeRecordListState state;
    state.actorUuid          = candidate.uuid;
    state.actorName          = candidate.name;
    state.title              = i18n.get("trade_records.target_title", {{"player", candidate.name}});
    state.allowPlayerKeyword = false;
    state.onBack             = onBack;
    showTradeRecordListForm(player, state);
}

std::vector<PlayerLookupCandidate> findFuzzyPlayerLookupCandidates(const std::string& keyword) {
    std::vector<PlayerLookupCandidate> candidates;
    if (keyword.empty()) {
        return candidates;
    }

    auto actorUuids = ShopRepository::getInstance().getDistinctTradeActorUuids();
    if (actorUuids.empty()) {
        return candidates;
    }

    auto nameCache = FormUtils::getPlayerNameCache(actorUuids);
    const std::string               unknownOwner = I18nService::getInstance().get("public_shop.unknown_owner");

    for (const auto& actorUuid : actorUuids) {
        if (actorUuid.empty()) {
            continue;
        }

        auto nameIt = nameCache.find(actorUuid);
        if (nameIt == nameCache.end()) {
            continue;
        }

        const std::string& actorName = nameIt->second;
        if (actorName.empty() || actorName == unknownOwner) {
            continue;
        }

        if (fuzzyMatch(actorName, keyword)) {
            candidates.push_back({actorUuid, actorName});
        }
    }

    const std::string lowerKeyword = toLower(keyword);
    std::sort(candidates.begin(), candidates.end(), [&lowerKeyword](const PlayerLookupCandidate& a, const PlayerLookupCandidate& b) {
        const bool exactA = toLower(a.name) == lowerKeyword;
        const bool exactB = toLower(b.name) == lowerKeyword;
        if (exactA != exactB) {
            return exactA;
        }
        return a.name < b.name;
    });

    return candidates;
}

std::vector<PlayerLookupCandidate> getOnlinePlayerLookupCandidates() {
    std::vector<PlayerLookupCandidate> candidates;
    auto                              level = ll::service::getLevel();
    if (!level) {
        return candidates;
    }

    level->forEachPlayer([&candidates](Player& onlinePlayer) {
        candidates.push_back({onlinePlayer.getUuid().asString(), onlinePlayer.getRealName()});
        return true;
    });

    std::sort(candidates.begin(), candidates.end(), [](const PlayerLookupCandidate& a, const PlayerLookupCandidate& b) {
        return a.name < b.name;
    });
    return candidates;
}

void showPlayerLookupCandidateForm(
    Player&                                 player,
    std::vector<PlayerLookupCandidate>      candidates,
    std::function<void(Player&)>            onBack,
    const std::string&                      keyword
) {
    auto&                i18n = I18nService::getInstance();
    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get("trade_records.lookup_select_title"));
    fm.setContent(i18n.get(
        "trade_records.lookup_select_content",
        {
            {"keyword", keyword}
        }
    ));

    for (const auto& candidate : candidates) {
        fm.appendButton(candidate.name, [candidate, onBack](Player& p) { openPlayerTradeRecords(p, candidate, onBack); });
    }

    fm.appendButton(i18n.get("form.button_back"), "textures/ui/arrow_left", "path", [onBack, keyword](Player& p) {
        showPlayerLookupForm(p, onBack, keyword);
    });
    fm.sendTo(player);
}

void showTradeRecordDetailForm(Player& player, const DisplayTradeRecord& record, const TradeRecordListState& state) {
    auto&                i18n = I18nService::getInstance();
    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get("trade_records.detail_title"));

    std::string content = i18n.get("trade_records.detail_type", {{"type", getTradeTypeLabel(record.raw.kind)}});
    content += "\n" + i18n.get("trade_records.detail_time", {{"time", record.raw.timestamp}});
    content += "\n" + i18n.get("trade_records.detail_player", {{"player", record.actorName}});
    content += "\n" + i18n.get("trade_records.detail_shop", {{"shop", record.shopDisplayName}});
    content += "\n" + i18n.get("trade_records.detail_item", {{"item", record.itemName}});
    content += "\n" + i18n.get("trade_records.detail_count", {{"count", std::to_string(record.raw.tradeCount)}});
    content += "\n" + i18n.get("trade_records.detail_unit_price", {{"price", MoneyFormat::format(record.unitPrice)}});
    content += "\n" + i18n.get("trade_records.detail_total_price", {{"price", MoneyFormat::format(record.raw.totalPrice)}});
    content += "\n" + i18n.get(
        "trade_records.detail_location",
        {
            {"dim", FormUtils::dimIdToString(record.raw.dimId)},
            {"x",   std::to_string(record.raw.pos.x)},
            {"y",   std::to_string(record.raw.pos.y)},
            {"z",   std::to_string(record.raw.pos.z)}
        }
    );
    if (record.raw.isOfficial) {
        content += "\n" + i18n.get("trade_records.detail_official");
    }
    fm.setContent(content);

    if (state.allowRecordTeleport && record.raw.kind == TradeRecordKind::Purchase) {
        fm.appendButton(
            i18n.get("trade_records.button_teleport_record"),
            "textures/ui/flyingascend_pressed",
            "path",
            [record](Player& p) {
                if (FormUtils::teleportToShop(p, record.raw.pos, record.raw.dimId)) {
                    p.sendMessage(I18nService::getInstance().get("public_shop.teleport_hint"));
                }
            }
        );
    }

    fm.appendButton(i18n.get("form.button_back"), "textures/ui/arrow_left", "path", [state](Player& p) {
        showTradeRecordListForm(p, state);
    });
    fm.sendTo(player);
}

void showTradeRecordSearchForm(Player& player, TradeRecordListState state) {
    auto&                i18n = I18nService::getInstance();
    ll::form::CustomForm fm;
    fm.setTitle(i18n.get("trade_records.search_title"));

    auto keywordFields = getAllowedKeywordFields(state);
    auto keywordLabels = getKeywordFieldLabels(keywordFields);
    std::vector<std::string> tradeTypeLabels = {
        i18n.get("trade_records.filter_all_types"),
        i18n.get("trade_records.filter_purchase_only"),
        i18n.get("trade_records.filter_recycle_only")
    };
    std::vector<std::string> officialTypeLabels = {
        i18n.get("trade_records.official_all"),
        i18n.get("trade_records.official_only"),
        i18n.get("trade_records.player_only")
    };
    int  keywordIndex  = 0;
    for (size_t i = 0; i < keywordFields.size(); ++i) {
        if (keywordFields[i] == state.keywordField) {
            keywordIndex = static_cast<int>(i);
            break;
        }
    }

    fm.appendDropdown(
        "trade_type",
        i18n.get("trade_records.search_trade_type"),
        tradeTypeLabels,
        static_cast<int>(state.typeFilter)
    );
    fm.appendDropdown("keyword_type", i18n.get("trade_records.search_keyword_type"), keywordLabels, keywordIndex);
    fm.appendInput("keyword", i18n.get("trade_records.search_keyword"), i18n.get("trade_records.search_keyword_hint"), state.keyword);

    if (state.allowOfficialFilter) {
        fm.appendDropdown(
            "official_type",
            i18n.get("trade_records.search_official_type"),
            officialTypeLabels,
            static_cast<int>(state.officialFilter)
        );
    }

    fm.sendTo(
        player,
        [state, keywordFields, keywordLabels, tradeTypeLabels, officialTypeLabels](
            Player& p,
            const ll::form::CustomFormResult& result,
            ll::form::FormCancelReason
        ) mutable {
        if (!result.has_value() || result->empty()) {
            showTradeRecordListForm(p, state);
            return;
        }

        state.currentPage  = 0;
        state.typeFilter   = static_cast<TradeRecordTypeFilter>(
            getDropdownIndex(result, "trade_type", tradeTypeLabels, static_cast<int>(state.typeFilter))
        );
        int keywordIndex   = getDropdownIndex(result, "keyword_type", keywordLabels, 0);
        if (keywordIndex >= 0 && keywordIndex < static_cast<int>(keywordFields.size())) {
            state.keywordField = keywordFields[static_cast<size_t>(keywordIndex)];
        }

        auto keywordIt = result->find("keyword");
        if (keywordIt != result->end()) {
            if (auto* value = std::get_if<std::string>(&keywordIt->second)) {
                state.keyword = *value;
            }
        }

        if (state.allowOfficialFilter) {
            state.officialFilter = static_cast<OfficialFilter>(
                getDropdownIndex(result, "official_type", officialTypeLabels, static_cast<int>(state.officialFilter))
            );
        }

        showTradeRecordListForm(p, state);
        }
    );
}

void showPlayerLookupForm(Player& player, std::function<void(Player&)> onBack, const std::string& initialKeyword) {
    auto&                i18n = I18nService::getInstance();
    ll::form::CustomForm fm;
    fm.setTitle(i18n.get("trade_records.lookup_title"));
    auto onlinePlayers = getOnlinePlayerLookupCandidates();
    std::vector<std::string> playerOptions;
    fm.appendInput(
        "player_name",
        i18n.get("trade_records.lookup_player_name"),
        i18n.get("trade_records.lookup_player_hint"),
        initialKeyword
    );
    if (!onlinePlayers.empty()) {
        playerOptions.reserve(onlinePlayers.size() + 1);
        playerOptions.push_back(i18n.get("trade_records.lookup_online_placeholder"));
        for (const auto& onlinePlayer : onlinePlayers) {
            playerOptions.push_back(onlinePlayer.name);
        }
        fm.appendDropdown("online_player", i18n.get("trade_records.lookup_online_player"), playerOptions, 0);
    }

    fm.sendTo(
        player,
        [onBack, onlinePlayers, playerOptions](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
            auto& i18n = I18nService::getInstance();
            if (!result.has_value() || result->empty()) {
                if (onBack) onBack(p);
                return;
            }

            std::string playerName;
            auto        it = result->find("player_name");
            if (it != result->end()) {
                if (auto* value = std::get_if<std::string>(&it->second)) {
                    playerName = *value;
                }
            }
            playerName = trimCopy(playerName);

            int selectedOnlineIndex = getDropdownIndex(result, "online_player", playerOptions, 0);
            if (playerName.empty() && selectedOnlineIndex > 0
                && selectedOnlineIndex <= static_cast<int>(onlinePlayers.size())) {
                openPlayerTradeRecords(p, onlinePlayers[static_cast<size_t>(selectedOnlineIndex - 1)], onBack);
                return;
            }

            if (playerName.empty()) {
                p.sendMessage(i18n.get("trade_records.player_not_found"));
                if (onBack) onBack(p);
                return;
            }

            auto playerInfo = ll::service::PlayerInfo::getInstance().fromName(playerName);

            if (playerInfo) {
                openPlayerTradeRecords(p, {playerInfo->uuid.asString(), playerInfo->name}, onBack);
                return;
            }

            auto candidates = findFuzzyPlayerLookupCandidates(playerName);
            if (candidates.empty()) {
                p.sendMessage(i18n.get("trade_records.player_not_found"));
                if (onBack) onBack(p);
                return;
            }

            if (candidates.size() == 1) {
                openPlayerTradeRecords(p, candidates.front(), onBack);
                return;
            }

            showPlayerLookupCandidateForm(p, std::move(candidates), onBack, playerName);
        }
    );
}

void showTradeRecordListForm(Player& player, TradeRecordListState state) {
    auto&                i18n = I18nService::getInstance();
    ll::form::SimpleForm fm;
    fm.setTitle(state.title);

    TradeRecordQuery query;
    query.includePurchase = state.typeFilter != TradeRecordTypeFilter::Recycle;
    query.includeRecycle  = state.typeFilter != TradeRecordTypeFilter::Purchase;
    query.actorUuid       = state.actorUuid;
    query.dimId           = state.dimId;
    query.pos             = state.pos;
    query.itemId          = state.itemId;
    if (state.allowOfficialFilter && state.officialFilter != OfficialFilter::All) {
        query.officialOnly = state.officialFilter == OfficialFilter::Official;
    }

    auto allRecords = ShopRepository::getInstance().getTradeRecords(query);

    std::vector<DisplayTradeRecord> pageRecords;
    int                             totalRecords = 0;
    int totalPages   = (totalRecords + RECORDS_PER_PAGE - 1) / RECORDS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;
    state.currentPage = std::max(0, std::min(state.currentPage, totalPages - 1));
    int startIndex    = 0;
    int endIndex      = 0;

    if (state.keyword.empty()) {
        totalRecords = static_cast<int>(allRecords.size());
        totalPages   = (totalRecords + RECORDS_PER_PAGE - 1) / RECORDS_PER_PAGE;
        if (totalPages == 0) totalPages = 1;
        state.currentPage = std::max(0, std::min(state.currentPage, totalPages - 1));

        startIndex = state.currentPage * RECORDS_PER_PAGE;
        endIndex   = std::min(startIndex + RECORDS_PER_PAGE, totalRecords);

        if (startIndex < endIndex) {
            std::vector<TradeRecordData> pageRawRecords;
            pageRawRecords.reserve(endIndex - startIndex);
            pageRawRecords.insert(
                pageRawRecords.end(),
                allRecords.begin() + startIndex,
                allRecords.begin() + endIndex
            );
            pageRecords = resolveTradeRecords(pageRawRecords);
        }
    } else {
        auto displayRecords = resolveTradeRecords(allRecords);

        std::vector<DisplayTradeRecord> filteredRecords;
        filteredRecords.reserve(displayRecords.size());
        for (const auto& record : displayRecords) {
            if (!matchesKeyword(record, state)) continue;
            filteredRecords.push_back(record);
        }

        totalRecords = static_cast<int>(filteredRecords.size());
        totalPages   = (totalRecords + RECORDS_PER_PAGE - 1) / RECORDS_PER_PAGE;
        if (totalPages == 0) totalPages = 1;
        state.currentPage = std::max(0, std::min(state.currentPage, totalPages - 1));

        startIndex = state.currentPage * RECORDS_PER_PAGE;
        endIndex   = std::min(startIndex + RECORDS_PER_PAGE, totalRecords);
        if (startIndex < endIndex) {
            pageRecords.assign(filteredRecords.begin() + startIndex, filteredRecords.begin() + endIndex);
        }
    }

    fm.appendButton(
        i18n.get("trade_records.button_search_filter"),
        "textures/ui/magnifyingGlass",
        "path",
        [state](Player& p) { showTradeRecordSearchForm(p, state); }
    );

    if (totalRecords == 0) {
        fm.setContent(i18n.get("trade_records.no_records"));
    } else {
        std::string content = buildListContent(state, totalRecords, state.currentPage, totalPages);
        content += "\n\n";

        for (int i = 0; i < static_cast<int>(pageRecords.size()); ++i) {
            content += buildAnnouncementEntry(pageRecords[i], startIndex + i + 1);
            content += "\n\n";
        }
        fm.setContent(content);
    }

    if (totalPages > 1) {
        if (state.currentPage > 0) {
            fm.appendButton(
                i18n.get("public_shop.button_prev_page"),
                "textures/ui/arrow_left",
                "path",
                [state](Player& p) mutable {
                    --state.currentPage;
                    showTradeRecordListForm(p, state);
                }
            );
        }
        if (state.currentPage < totalPages - 1) {
            fm.appendButton(
                i18n.get("public_shop.button_next_page"),
                "textures/ui/arrow_right",
                "path",
                [state](Player& p) mutable {
                    ++state.currentPage;
                    showTradeRecordListForm(p, state);
                }
            );
        }
    }

    fm.appendButton(i18n.get("form.button_back"), "textures/ui/arrow_left", "path", [state](Player& p) {
        if (state.onBack) {
            state.onBack(p);
        }
    });
    fm.sendTo(player);
}

} // namespace

void showTradeRecordMenuForm(Player& player, std::function<void(Player&)> onBack, bool showLastPurchase) {
    auto&                i18n = I18nService::getInstance();
    ll::form::SimpleForm fm;
    bool                 admin = isAdmin(player);

    fm.setTitle(i18n.get("trade_records.menu_title"));
    fm.setContent(i18n.get(admin ? "trade_records.menu_content_admin" : "trade_records.menu_content"));

    fm.appendButton(i18n.get("trade_records.button_personal_records"), [onBack, showLastPurchase](Player& p) {
        showPlayerTradeRecordsForm(
            p,
            [onBack, showLastPurchase](Player& backPlayer) {
                showTradeRecordMenuForm(backPlayer, onBack, showLastPurchase);
            }
        );
    });

    fm.appendButton(i18n.get("trade_records.button_global_records"), [onBack, showLastPurchase](Player& p) {
        TradeRecordListState state;
        state.title  = I18nService::getInstance().get("trade_records.global_title");
        state.onBack = [onBack, showLastPurchase](Player& backPlayer) {
            showTradeRecordMenuForm(backPlayer, onBack, showLastPurchase);
        };
        showTradeRecordListForm(p, state);
    });

    if (showLastPurchase) {
        fm.appendButton(i18n.get("trade_records.button_last_purchase"), [onBack, showLastPurchase](Player& p) {
            showLastPurchasePreviewForm(
                p,
                [onBack, showLastPurchase](Player& backPlayer) {
                    showTradeRecordMenuForm(backPlayer, onBack, showLastPurchase);
                }
            );
        });
    }

    fm.appendButton(i18n.get("trade_records.button_lookup_player"), [onBack, showLastPurchase](Player& p) {
        showPlayerLookupForm(
            p,
            [onBack, showLastPurchase](Player& backPlayer) {
                showTradeRecordMenuForm(backPlayer, onBack, showLastPurchase);
            }
        );
    });

    fm.appendButton(i18n.get("form.button_back"), "textures/ui/arrow_left", "path", [onBack](Player& p) {
        if (onBack) onBack(p);
    });
    fm.sendTo(player);
}

void showPlayerTradeRecordsForm(Player& player, std::function<void(Player&)> onBack) {
    auto& i18n = I18nService::getInstance();

    TradeRecordListState state;
    state.actorUuid          = player.getUuid().asString();
    state.actorName          = player.getRealName();
    state.title              = i18n.get("trade_records.personal_title");
    state.allowPlayerKeyword = false;
    state.onBack             = onBack;
    showTradeRecordListForm(player, state);
}

void showShopTradeRecordsForm(Player& player, BlockPos pos, int dimId, std::function<void(Player&)> onBack) {
    auto& i18n = I18nService::getInstance();

    TradeRecordListState state;
    state.title               = i18n.get("trade_records.shop_title");
    state.typeFilter          = TradeRecordTypeFilter::Purchase;
    state.dimId               = dimId;
    state.pos                 = pos;
    state.allowOfficialFilter = false;
    state.allowShopKeyword    = false;
    state.onBack              = onBack;
    showTradeRecordListForm(player, state);
}

void showRecycleShopTradeRecordsForm(Player& player, BlockPos pos, int dimId, std::function<void(Player&)> onBack) {
    auto& i18n = I18nService::getInstance();

    TradeRecordListState state;
    state.title               = i18n.get("trade_records.recycle_shop_title");
    state.typeFilter          = TradeRecordTypeFilter::Recycle;
    state.dimId               = dimId;
    state.pos                 = pos;
    state.allowOfficialFilter = false;
    state.allowShopKeyword    = false;
    state.onBack              = onBack;
    showTradeRecordListForm(player, state);
}

} // namespace CT
