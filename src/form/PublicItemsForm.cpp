#include "PublicItemsForm.h"
#include "Config/ConfigManager.h"
#include "FormUtils.h"
#include "ShopForm.h"
#include "TradeRecordForm.h"
#include "Utils/MoneyFormat.h"
#include "Utils/NbtUtils.h"
#include "Utils/Pagination.h"
#include "chestui/chestui.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/form/SimpleForm.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/PlayerInfo.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "mc/platform/UUID.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "repository/ShopRepository.h"
#include "service/I18nService.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>

namespace CT {

static const int ITEMS_PER_PAGE = 10;
static constexpr size_t ITEMS_CHEST_UI_PER_PAGE = 45;
static constexpr size_t ITEMS_CHEST_UI_PREV_SLOT = 45;
static constexpr size_t ITEMS_CHEST_UI_INFO_SLOT = 46;
static constexpr size_t ITEMS_CHEST_UI_NEXT_SLOT = 47;
static constexpr size_t ITEMS_CHEST_UI_SEARCH_SLOT = 48;
static constexpr size_t ITEMS_CHEST_UI_RECORDS_SLOT = 49;
static constexpr size_t ITEMS_CHEST_UI_CLOSE_SLOT = 50;
static constexpr size_t ITEMS_CHEST_UI_BACK_SLOT = 51;
static constexpr int    ITEMS_CHEST_UI_OPEN_DELAY_TICKS = 4;

// 前向声明
static void showShopItemDetailForm(
    Player&                      player,
    const PublicShopItemData&    item,
    std::function<void(Player&)> onBack = {}
);
static void showRecycleItemDetailForm(
    Player&                      player,
    const PublicRecycleItemData& item,
    std::function<void(Player&)> onBack = {}
);

static std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
    return result;
}

static bool fuzzyMatch(const std::string& text, const std::string& lowerKeyword) {
    return toLower(text).find(lowerKeyword) != std::string::npos;
}

static std::string escapeSnbtString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());

    for (char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += ch;
            break;
        }
    }

    return escaped;
}

static std::unique_ptr<CompoundTag> buildDisplayTag(const std::string& name, const std::vector<std::string>& loreLines) {
    std::string snbt = "{Name:\"" + escapeSnbtString(name) + "\",Lore:[";
    for (size_t i = 0; i < loreLines.size(); ++i) {
        if (i > 0) {
            snbt += ",";
        }
        snbt += "\"" + escapeSnbtString(loreLines[i]) + "\"";
    }
    snbt += "]}";
    return NbtUtils::parseSNBT(snbt);
}

static bool applyDisplayTag(ItemStack& item, const std::string& name, const std::vector<std::string>& loreLines) {
    auto itemNbt = NbtUtils::getItemNbt(item);
    if (!itemNbt) {
        return false;
    }

    CompoundTag tagNbt;
    if (itemNbt->contains("tag")) {
        tagNbt = itemNbt->at("tag").get<CompoundTag>();
    }

    auto displayTag = buildDisplayTag(name, loreLines);
    if (!displayTag) {
        return false;
    }

    tagNbt["display"] = *displayTag;
    (*itemNbt)["tag"] = std::move(tagNbt);
    return NbtUtils::setItemNbt(item, *itemNbt);
}

static ItemStack makeChestUiControlItem(
    std::string_view                typeName,
    const std::string&              displayName,
    const std::vector<std::string>& loreLines = {}
) {
    ItemStack item;
    item.reinit(typeName, 1, 0);
    applyDisplayTag(item, displayName, loreLines);
    return item;
}

static Player* findOnlinePlayer(const std::string& uuidString) {
    auto level = ll::service::getLevel();
    if (!level) {
        return nullptr;
    }

    auto uuid = mce::UUID::fromString(uuidString);
    if (uuid == mce::UUID::EMPTY()) {
        return nullptr;
    }

    return level->getPlayer(uuid);
}

static void runAfterTicks(int ticks, std::function<void()> task) {
    if (ticks <= 0) {
        ll::thread::ServerThreadExecutor::getDefault().execute(std::move(task));
        return;
    }

    ll::thread::ServerThreadExecutor::getDefault().executeAfter(std::move(task), std::chrono::milliseconds(ticks * 50));
}

static void runForOnlinePlayerAfterTicks(Player& player, int ticks, std::function<void(Player&)> task) {
    auto playerUuid = player.getUuid().asString();
    runAfterTicks(ticks, [playerUuid, task = std::move(task)]() mutable {
        if (auto* target = findOnlinePlayer(playerUuid)) {
            task(*target);
        }
    });
}

static std::string buildDetailTeleportCostText() {
    double teleportCost = ConfigManager::getInstance().get().teleportSettings.teleportCost;
    if (teleportCost <= 0.0) {
        return {};
    }

    return I18nService::getInstance().get(
        "public_shop.preview_teleport_cost",
        {{"cost", CT::MoneyFormat::format(teleportCost)}}
    );
}

static std::string buildRecycleRemainingText(const PublicRecycleItemData& item) {
    auto& i18n = I18nService::getInstance();
    if (item.maxRecycleCount <= 0) {
        return i18n.get("public_items.recycle_unlimited");
    }

    int remaining = std::max(0, item.maxRecycleCount - item.currentRecycledCount);
    return i18n.get("public_items.recycle_remaining", {{"count", std::to_string(remaining)}});
}

static std::string resolveOwnerDisplayName(const std::string& ownerUuid) {
    if (ownerUuid.empty()) {
        return {};
    }

    auto ownerNameCache = CT::FormUtils::getPlayerNameCache({ownerUuid});
    auto it = ownerNameCache.find(ownerUuid);
    if (it != ownerNameCache.end() && !it->second.empty()) {
        return it->second;
    }

    return I18nService::getInstance().get("public_shop.unknown_owner");
}

static std::vector<PublicShopItemData> filterPublicShopItems(const std::string& searchKeyword, const std::string& ownerUuid = {}) {
    auto allItems = ShopRepository::getInstance().findAllPublicShopItems();

    std::vector<PublicShopItemData> filteredItems;
    std::string lowerKeyword = searchKeyword.empty() ? std::string{} : toLower(searchKeyword);
    for (const auto& item : allItems) {
        if (item.dbCount <= 0) continue;
        if (!ownerUuid.empty() && item.ownerUuid != ownerUuid) continue;
        if (!lowerKeyword.empty()) {
            auto itemPtr = CT::FormUtils::createItemStackFromNbtString(item.itemNbt);
            if (!itemPtr) continue;
            std::string itemName = itemPtr->getName();
            std::string typeName = itemPtr->getTypeName();
            if (!fuzzyMatch(itemName, lowerKeyword) && !fuzzyMatch(typeName, lowerKeyword)) {
                continue;
            }
        }
        filteredItems.push_back(item);
    }

    return filteredItems;
}

struct PublicItemsChestUiPageData {
    std::string                   title;
    std::vector<ItemStack>        items;
    std::vector<PublicShopItemData> entries;
    size_t                        currentPage{0};
    size_t                        totalPages{1};
};

struct PublicRecycleItemsChestUiPageData {
    std::string                      title;
    std::vector<ItemStack>           items;
    std::vector<PublicRecycleItemData> entries;
    size_t                           currentPage{0};
    size_t                           totalPages{1};
};

static std::string trimTrailingNewline(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

static std::vector<PublicRecycleItemData> filterPublicRecycleItems(
    const std::string& searchKeyword,
    const std::string& ownerUuid = {}
) {
    auto allItems = ShopRepository::getInstance().findAllPublicRecycleItems();

    std::vector<PublicRecycleItemData> filteredItems;
    std::string lowerKeyword = searchKeyword.empty() ? std::string{} : toLower(searchKeyword);
    for (const auto& item : allItems) {
        if (!ownerUuid.empty() && item.ownerUuid != ownerUuid) continue;
        if (!lowerKeyword.empty()) {
            auto itemPtr = CT::FormUtils::createItemStackFromNbtString(item.itemNbt);
            if (!itemPtr) continue;
            std::string itemName = itemPtr->getName();
            std::string typeName = itemPtr->getTypeName();
            if (!fuzzyMatch(itemName, lowerKeyword) && !fuzzyMatch(typeName, lowerKeyword)) {
                continue;
            }
        }
        filteredItems.push_back(item);
    }

    return filteredItems;
}

static PublicItemsChestUiPageData
buildPublicItemsChestUiPage(const std::string& searchKeyword, size_t requestedPage, const std::string& ownerUuid = {}) {
    auto& i18n          = I18nService::getInstance();
    auto  filteredItems = filterPublicShopItems(searchKeyword, ownerUuid);

    auto pageSlice =
        Pagination::makeZeroBasedPageSlice(static_cast<int>(filteredItems.size()), ITEMS_CHEST_UI_PER_PAGE, (int)requestedPage);
    size_t currentPage = static_cast<size_t>(pageSlice.currentPage);
    size_t totalPages  = static_cast<size_t>(pageSlice.totalPages);

    std::vector<ItemStack> chestItems(54, ItemStack::EMPTY_ITEM());
    std::vector<PublicShopItemData> entries;
    entries.reserve(pageSlice.endIndex - pageSlice.startIndex);

    std::vector<std::string> ownerUuids;
    for (int index = pageSlice.startIndex; index < pageSlice.endIndex; ++index) {
        ownerUuids.push_back(filteredItems[index].ownerUuid);
    }
    auto ownerNameCache = CT::FormUtils::getPlayerNameCache(ownerUuids);

    for (int index = pageSlice.startIndex; index < pageSlice.endIndex; ++index) {
        const auto& item = filteredItems[index];
        auto itemPtr = CT::FormUtils::createItemStackFromNbtString(item.itemNbt);
        if (!itemPtr) {
            continue;
        }

        std::string ownerName = ownerNameCache[item.ownerUuid];
        if (ownerName.empty()) {
            ownerName = i18n.get("public_shop.unknown_owner");
        }
        std::string shopDisplayName = item.shopName.empty()
            ? i18n.get("public_shop.owner_shop", {{"owner", ownerName}})
            : item.shopName;

        ItemStack displayItem = *itemPtr;
        displayItem.set(std::clamp(item.dbCount, 1, 64));
        std::string itemName = std::string(displayItem.getName());
        if (itemName.empty()) {
            itemName = displayItem.getTypeName();
        }
        if (item.isOfficial) {
            itemName = i18n.get("public_shop.official_tag") + " §r" + itemName;
        }
        applyDisplayTag(
            displayItem,
            itemName,
            {
                i18n.get("form.chest_ui_item_price", {{"price", CT::MoneyFormat::format(item.price)}}),
                i18n.get("form.chest_ui_item_stock", {{"stock", std::to_string(item.dbCount)}}),
                trimTrailingNewline(i18n.get("public_shop.preview_owner", {{"owner", ownerName}})),
                trimTrailingNewline(i18n.get("public_items.item_shop", {{"shop", shopDisplayName}})),
                i18n.get("form.chest_ui_item_hint")
            }
        );

        chestItems[entries.size()] = std::move(displayItem);
        entries.push_back(item);
    }

    if (currentPage > 0) {
        chestItems[ITEMS_CHEST_UI_PREV_SLOT] =
            makeChestUiControlItem("minecraft:arrow", i18n.get("public_shop.button_prev_page"));
    }
    chestItems[ITEMS_CHEST_UI_INFO_SLOT] = makeChestUiControlItem(
        "minecraft:book",
        i18n.get(
            "form.chest_ui_page_info",
            {{"page", std::to_string(currentPage + 1)}, {"total", std::to_string(std::max<size_t>(1, totalPages))}}
        ),
        {i18n.get(
            "public_items.total_items",
            {
                {"count", std::to_string(filteredItems.size())   },
                {"page",  std::to_string(currentPage + 1)        },
                {"total", std::to_string(std::max<size_t>(1, totalPages))}
            }
        )}
    );
    if (currentPage + 1 < std::max<size_t>(1, totalPages)) {
        chestItems[ITEMS_CHEST_UI_NEXT_SLOT] =
            makeChestUiControlItem("minecraft:arrow", i18n.get("public_shop.button_next_page"));
    }
    chestItems[ITEMS_CHEST_UI_SEARCH_SLOT] =
        makeChestUiControlItem("minecraft:compass", i18n.get("public_shop.button_search"));
    chestItems[ITEMS_CHEST_UI_RECORDS_SLOT] =
        makeChestUiControlItem("minecraft:book", i18n.get("form.button_trade_records"));
    chestItems[ITEMS_CHEST_UI_CLOSE_SLOT] =
        makeChestUiControlItem("minecraft:barrier", i18n.get("public_shop.button_close"));

    std::string title = ownerUuid.empty()
        ? i18n.get("public_items.list_title")
        : i18n.get("public_items.owner_list_title", {{"owner", resolveOwnerDisplayName(ownerUuid)}});
    title += " §7(" + std::to_string(currentPage + 1) + "/" + std::to_string(std::max<size_t>(1, totalPages)) + ")§r";
    if (!searchKeyword.empty()) {
        title += " §6" + searchKeyword + "§r";
    }

    return PublicItemsChestUiPageData{
        .title       = std::move(title),
        .items       = std::move(chestItems),
        .entries     = std::move(entries),
        .currentPage = currentPage,
        .totalPages  = std::max<size_t>(1, totalPages)
    };
}

static PublicRecycleItemsChestUiPageData
buildPublicRecycleItemsChestUiPage(
    const std::string& searchKeyword,
    size_t             requestedPage,
    const std::string& ownerUuid = {}
) {
    auto& i18n          = I18nService::getInstance();
    auto  filteredItems = filterPublicRecycleItems(searchKeyword, ownerUuid);

    auto pageSlice =
        Pagination::makeZeroBasedPageSlice(static_cast<int>(filteredItems.size()), ITEMS_CHEST_UI_PER_PAGE, (int)requestedPage);
    size_t currentPage = static_cast<size_t>(pageSlice.currentPage);
    size_t totalPages  = static_cast<size_t>(pageSlice.totalPages);

    std::vector<ItemStack> chestItems(54, ItemStack::EMPTY_ITEM());
    std::vector<PublicRecycleItemData> entries;
    entries.reserve(pageSlice.endIndex - pageSlice.startIndex);

    std::vector<std::string> ownerUuids;
    for (int index = pageSlice.startIndex; index < pageSlice.endIndex; ++index) {
        ownerUuids.push_back(filteredItems[index].ownerUuid);
    }
    auto ownerNameCache = CT::FormUtils::getPlayerNameCache(ownerUuids);

    for (int index = pageSlice.startIndex; index < pageSlice.endIndex; ++index) {
        const auto& item = filteredItems[index];
        auto itemPtr = CT::FormUtils::createItemStackFromNbtString(item.itemNbt);
        if (!itemPtr) {
            continue;
        }

        std::string ownerName = ownerNameCache[item.ownerUuid];
        if (ownerName.empty()) {
            ownerName = i18n.get("public_shop.unknown_owner");
        }
        std::string shopDisplayName = item.shopName.empty()
            ? i18n.get("public_shop.owner_recycle_shop", {{"owner", ownerName}})
            : item.shopName;

        ItemStack displayItem = *itemPtr;
        displayItem.set(1);
        std::string itemName = std::string(displayItem.getName());
        if (itemName.empty()) {
            itemName = displayItem.getTypeName();
        }
        if (item.isOfficial) {
            itemName = i18n.get("public_shop.official_tag") + " §r" + itemName;
        }
        applyDisplayTag(
            displayItem,
            itemName,
            {
                i18n.get("form.chest_ui_item_price", {{"price", CT::MoneyFormat::format(item.price)}}),
                trimTrailingNewline(buildRecycleRemainingText(item)),
                trimTrailingNewline(i18n.get("public_shop.preview_owner", {{"owner", ownerName}})),
                trimTrailingNewline(i18n.get("public_items.item_shop", {{"shop", shopDisplayName}})),
                i18n.get("form.chest_ui_item_hint")
            }
        );

        chestItems[entries.size()] = std::move(displayItem);
        entries.push_back(item);
    }

    if (currentPage > 0) {
        chestItems[ITEMS_CHEST_UI_PREV_SLOT] =
            makeChestUiControlItem("minecraft:arrow", i18n.get("public_shop.button_prev_page"));
    }
    chestItems[ITEMS_CHEST_UI_INFO_SLOT] = makeChestUiControlItem(
        "minecraft:book",
        i18n.get(
            "form.chest_ui_page_info",
            {{"page", std::to_string(currentPage + 1)}, {"total", std::to_string(std::max<size_t>(1, totalPages))}}
        ),
        {i18n.get(
            "public_items.total_items",
            {
                {"count", std::to_string(filteredItems.size())   },
                {"page",  std::to_string(currentPage + 1)        },
                {"total", std::to_string(std::max<size_t>(1, totalPages))}
            }
        )}
    );
    if (currentPage + 1 < std::max<size_t>(1, totalPages)) {
        chestItems[ITEMS_CHEST_UI_NEXT_SLOT] =
            makeChestUiControlItem("minecraft:arrow", i18n.get("public_shop.button_next_page"));
    }
    chestItems[ITEMS_CHEST_UI_SEARCH_SLOT] =
        makeChestUiControlItem("minecraft:compass", i18n.get("public_shop.button_search"));
    chestItems[ITEMS_CHEST_UI_RECORDS_SLOT] =
        makeChestUiControlItem("minecraft:book", i18n.get("form.button_trade_records"));
    chestItems[ITEMS_CHEST_UI_CLOSE_SLOT] =
        makeChestUiControlItem("minecraft:barrier", i18n.get("public_shop.button_close"));

    std::string title = ownerUuid.empty()
        ? i18n.get("public_items.recycle_list_title")
        : i18n.get("public_items.owner_recycle_list_title", {{"owner", resolveOwnerDisplayName(ownerUuid)}});
    title += " §7(" + std::to_string(currentPage + 1) + "/" + std::to_string(std::max<size_t>(1, totalPages)) + ")§r";
    if (!searchKeyword.empty()) {
        title += " §6" + searchKeyword + "§r";
    }

    return PublicRecycleItemsChestUiPageData{
        .title       = std::move(title),
        .items       = std::move(chestItems),
        .entries     = std::move(entries),
        .currentPage = currentPage,
        .totalPages  = std::max<size_t>(1, totalPages)
    };
}

static void showSearchForm(
    Player&            player,
    bool               returnToChestUi = false,
    int                currentPage     = 0,
    const std::string& currentKeyword  = "",
    const std::string& ownerUuid       = "",
    std::function<void(Player&)> onBack = {}
);
static void showRecycleSearchForm(
    Player&            player,
    bool               returnToChestUi = false,
    int                currentPage     = 0,
    const std::string& currentKeyword  = "",
    const std::string& ownerUuid       = "",
    std::function<void(Player&)> onBack = {}
);

void showPublicItemsForm(Player& player, int currentPage, const std::string& searchKeyword) {
    auto& i18n = I18nService::getInstance();

    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get("public_items.list_title"));

    auto filteredItems = filterPublicShopItems(searchKeyword);

    int  totalItems = static_cast<int>(filteredItems.size());
    auto pageSlice  = Pagination::makeZeroBasedPageSlice(totalItems, ITEMS_PER_PAGE, currentPage);
    int  totalPages = pageSlice.totalPages;
    currentPage     = pageSlice.currentPage;
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

        int startIdx = pageSlice.startIndex;
        int endIdx   = pageSlice.endIndex;

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

    fm.appendButton(i18n.get("public_shop.button_close"), "textures/ui/cancel", "path", [](Player&) {});
    fm.sendTo(player);
}

void showPublicItemsChestUi(Player& player, size_t currentPage, const std::string& searchKeyword) {
    showPublicItemsChestUi(player, currentPage, searchKeyword, {});
}

void showPublicItemsChestUi(
    Player&            player,
    size_t             currentPage,
    const std::string& searchKeyword,
    std::function<void(Player&)> onBack
) {
    struct PublicItemsChestUiState {
        std::string searchKeyword;
        std::string ownerUuid;
        std::function<void(Player&)> onBack;
        size_t      currentPage{0};
    };

    auto state = std::make_shared<PublicItemsChestUiState>(PublicItemsChestUiState{
        .searchKeyword = searchKeyword,
        .ownerUuid     = "",
        .onBack        = std::move(onBack),
        .currentPage   = currentPage
    });

    auto refreshView = std::make_shared<std::function<void(Player&, bool)>>();
    *refreshView     = [state, refreshView](Player& target, bool reopen) {
        auto pageData = buildPublicItemsChestUiPage(state->searchKeyword, state->currentPage, state->ownerUuid);
        state->currentPage = pageData.currentPage;
        if (state->onBack) {
            pageData.items[ITEMS_CHEST_UI_BACK_SLOT] =
                makeChestUiControlItem("minecraft:arrow", I18nService::getInstance().get("form.button_back"));
        }
        auto handleClick = [state, refreshView](Player& p, ChestUI::ClickContext const& ctx) {
            switch (ctx.slot) {
            case ITEMS_CHEST_UI_PREV_SLOT:
                if (state->currentPage > 0) {
                    --state->currentPage;
                    (*refreshView)(p, false);
                }
                return;
            case ITEMS_CHEST_UI_NEXT_SLOT: {
                auto currentData = buildPublicItemsChestUiPage(state->searchKeyword, state->currentPage, state->ownerUuid);
                if (state->currentPage + 1 < currentData.totalPages) {
                    ++state->currentPage;
                    (*refreshView)(p, false);
                }
                return;
            }
            case ITEMS_CHEST_UI_SEARCH_SLOT:
                ChestUI::close(p);
                runForOnlinePlayerAfterTicks(
                    p,
                    ITEMS_CHEST_UI_OPEN_DELAY_TICKS,
                    [keyword = state->searchKeyword,
                     ownerUuid = state->ownerUuid,
                     page = (int)state->currentPage,
                     onBack = state->onBack](Player& target) {
                        showSearchForm(target, true, page, keyword, ownerUuid, onBack);
                    }
                );
                return;
            case ITEMS_CHEST_UI_RECORDS_SLOT:
                ChestUI::close(p);
                runForOnlinePlayerAfterTicks(
                    p,
                    ITEMS_CHEST_UI_OPEN_DELAY_TICKS,
                    [keyword = state->searchKeyword,
                     ownerUuid = state->ownerUuid,
                     page = state->currentPage,
                     onBack = state->onBack](Player& target) {
                        showTradeRecordMenuForm(target, [keyword, ownerUuid, page, onBack](Player& backPlayer) {
                            if (ownerUuid.empty()) {
                                showPublicItemsChestUi(backPlayer, page, keyword, onBack);
                            } else {
                                showPlayerPublicItemsChestUi(backPlayer, ownerUuid, page, keyword, onBack);
                            }
                        }, false);
                    }
                );
                return;
            case ITEMS_CHEST_UI_CLOSE_SLOT:
                ChestUI::close(p);
                return;
            case ITEMS_CHEST_UI_BACK_SLOT:
                if (state->onBack) {
                    state->onBack(p);
                }
                return;
            default:
                break;
            }

            if (ctx.slot == ITEMS_CHEST_UI_INFO_SLOT || ctx.slot >= ITEMS_CHEST_UI_PER_PAGE) {
                return;
            }

            auto currentData = buildPublicItemsChestUiPage(state->searchKeyword, state->currentPage, state->ownerUuid);
            if (ctx.slot >= currentData.entries.size()) {
                return;
            }

            const auto item = currentData.entries[ctx.slot];
            ChestUI::close(p);
            runForOnlinePlayerAfterTicks(
                p,
                ITEMS_CHEST_UI_OPEN_DELAY_TICKS,
                [item,
                 keyword = state->searchKeyword,
                 ownerUuid = state->ownerUuid,
                 page = state->currentPage,
                 onBack = state->onBack](Player& target) {
                    showShopItemDetailForm(target, item, [keyword, ownerUuid, page, onBack](Player& backPlayer) {
                        if (ownerUuid.empty()) {
                            showPublicItemsChestUi(backPlayer, page, keyword, onBack);
                        } else {
                            showPlayerPublicItemsChestUi(backPlayer, ownerUuid, page, keyword, onBack);
                        }
                    });
                }
            );
        };

        if (reopen || !ChestUI::isOpen(target)) {
            ChestUI::OpenRequest request;
            request.title        = pageData.title;
            request.items        = pageData.items;
            request.onClick      = std::move(handleClick);
            request.onClose      = [](Player&) {};
            request.closeOnClick = false;
            if (!ChestUI::open(target, std::move(request))) {
                showPublicItemsForm(target, static_cast<int>(state->currentPage), state->searchKeyword);
            }
            return;
        }

        ChestUI::UpdateRequest request;
        request.title        = pageData.title;
        request.items        = pageData.items;
        request.onClick      = std::move(handleClick);
        request.onClose      = [](Player&) {};
        request.closeOnClick = false;
        if (!ChestUI::update(target, std::move(request))) {
            (*refreshView)(target, true);
        }
    };

    (*refreshView)(player, false);
}

void showPlayerPublicItemsChestUi(
    Player&            player,
    const std::string& ownerUuid,
    size_t             currentPage,
    const std::string& searchKeyword,
    std::function<void(Player&)> onBack
) {
    struct PublicItemsChestUiState {
        std::string searchKeyword;
        std::string ownerUuid;
        std::function<void(Player&)> onBack;
        size_t      currentPage{0};
    };

    auto state = std::make_shared<PublicItemsChestUiState>(PublicItemsChestUiState{
        .searchKeyword = searchKeyword,
        .ownerUuid     = ownerUuid,
        .onBack        = std::move(onBack),
        .currentPage   = currentPage
    });

    auto refreshView = std::make_shared<std::function<void(Player&, bool)>>();
    *refreshView     = [state, refreshView](Player& target, bool reopen) {
        auto pageData = buildPublicItemsChestUiPage(state->searchKeyword, state->currentPage, state->ownerUuid);
        state->currentPage = pageData.currentPage;
        auto handleClick = [state, refreshView](Player& p, ChestUI::ClickContext const& ctx) {
            switch (ctx.slot) {
            case ITEMS_CHEST_UI_PREV_SLOT:
                if (state->currentPage > 0) {
                    --state->currentPage;
                    (*refreshView)(p, false);
                }
                return;
            case ITEMS_CHEST_UI_NEXT_SLOT: {
                auto currentData = buildPublicItemsChestUiPage(state->searchKeyword, state->currentPage, state->ownerUuid);
                if (state->currentPage + 1 < currentData.totalPages) {
                    ++state->currentPage;
                    (*refreshView)(p, false);
                }
                return;
            }
            case ITEMS_CHEST_UI_SEARCH_SLOT:
                ChestUI::close(p);
                runForOnlinePlayerAfterTicks(
                    p,
                    ITEMS_CHEST_UI_OPEN_DELAY_TICKS,
                    [keyword = state->searchKeyword,
                     ownerUuid = state->ownerUuid,
                     page = (int)state->currentPage,
                     onBack = state->onBack](Player& target) {
                        showSearchForm(target, true, page, keyword, ownerUuid, onBack);
                    }
                );
                return;
            case ITEMS_CHEST_UI_RECORDS_SLOT:
                ChestUI::close(p);
                runForOnlinePlayerAfterTicks(
                    p,
                    ITEMS_CHEST_UI_OPEN_DELAY_TICKS,
                    [keyword = state->searchKeyword,
                     ownerUuid = state->ownerUuid,
                     page = state->currentPage,
                     onBack = state->onBack](Player& target) {
                        showTradeRecordMenuForm(target, [keyword, ownerUuid, page, onBack](Player& backPlayer) {
                            showPlayerPublicItemsChestUi(backPlayer, ownerUuid, page, keyword, onBack);
                        }, false);
                    }
                );
                return;
            case ITEMS_CHEST_UI_CLOSE_SLOT:
                ChestUI::close(p);
                return;
            case ITEMS_CHEST_UI_BACK_SLOT:
                if (state->onBack) {
                    state->onBack(p);
                }
                return;
            default:
                break;
            }

            if (ctx.slot == ITEMS_CHEST_UI_INFO_SLOT || ctx.slot >= ITEMS_CHEST_UI_PER_PAGE) {
                return;
            }

            auto currentData = buildPublicItemsChestUiPage(state->searchKeyword, state->currentPage, state->ownerUuid);
            if (ctx.slot >= currentData.entries.size()) {
                return;
            }

            const auto item = currentData.entries[ctx.slot];
            ChestUI::close(p);
            runForOnlinePlayerAfterTicks(
                p,
                ITEMS_CHEST_UI_OPEN_DELAY_TICKS,
                [item,
                 keyword = state->searchKeyword,
                 ownerUuid = state->ownerUuid,
                 page = state->currentPage,
                 onBack = state->onBack](Player& target) {
                    showShopItemDetailForm(target, item, [keyword, ownerUuid, page, onBack](Player& backPlayer) {
                        showPlayerPublicItemsChestUi(backPlayer, ownerUuid, page, keyword, onBack);
                    });
                }
            );
        };

        if (reopen || !ChestUI::isOpen(target)) {
            ChestUI::OpenRequest request;
            request.title        = pageData.title;
            request.items        = pageData.items;
            request.onClick      = std::move(handleClick);
            request.onClose      = [](Player&) {};
            request.closeOnClick = false;
            if (!ChestUI::open(target, std::move(request))) {
                showPublicItemsForm(target, static_cast<int>(state->currentPage), state->searchKeyword);
            }
            return;
        }

        ChestUI::UpdateRequest request;
        request.title        = pageData.title;
        request.items        = pageData.items;
        request.onClick      = std::move(handleClick);
        request.onClose      = [](Player&) {};
        request.closeOnClick = false;
        if (!ChestUI::update(target, std::move(request))) {
            (*refreshView)(target, true);
        }
    };

    (*refreshView)(player, false);
}

void showPublicRecycleItemsChestUi(Player& player, size_t currentPage, const std::string& searchKeyword) {
    showPublicRecycleItemsChestUi(player, currentPage, searchKeyword, {});
}

void showPublicRecycleItemsChestUi(
    Player&            player,
    size_t             currentPage,
    const std::string& searchKeyword,
    std::function<void(Player&)> onBack
) {
    struct PublicRecycleItemsChestUiState {
        std::string searchKeyword;
        std::string ownerUuid;
        std::function<void(Player&)> onBack;
        size_t      currentPage{0};
    };

    auto state = std::make_shared<PublicRecycleItemsChestUiState>(PublicRecycleItemsChestUiState{
        .searchKeyword = searchKeyword,
        .ownerUuid     = "",
        .onBack        = std::move(onBack),
        .currentPage   = currentPage
    });

    auto refreshView = std::make_shared<std::function<void(Player&, bool)>>();
    *refreshView     = [state, refreshView](Player& target, bool reopen) {
        auto pageData = buildPublicRecycleItemsChestUiPage(state->searchKeyword, state->currentPage, state->ownerUuid);
        state->currentPage = pageData.currentPage;
        if (state->onBack) {
            pageData.items[ITEMS_CHEST_UI_BACK_SLOT] =
                makeChestUiControlItem("minecraft:arrow", I18nService::getInstance().get("form.button_back"));
        }

        auto handleClick = [state, refreshView](Player& p, ChestUI::ClickContext const& ctx) {
            switch (ctx.slot) {
            case ITEMS_CHEST_UI_PREV_SLOT:
                if (state->currentPage > 0) {
                    --state->currentPage;
                    (*refreshView)(p, false);
                }
                return;
            case ITEMS_CHEST_UI_NEXT_SLOT: {
                auto currentData = buildPublicRecycleItemsChestUiPage(
                    state->searchKeyword,
                    state->currentPage,
                    state->ownerUuid
                );
                if (state->currentPage + 1 < currentData.totalPages) {
                    ++state->currentPage;
                    (*refreshView)(p, false);
                }
                return;
            }
            case ITEMS_CHEST_UI_SEARCH_SLOT:
                ChestUI::close(p);
                runForOnlinePlayerAfterTicks(
                    p,
                    ITEMS_CHEST_UI_OPEN_DELAY_TICKS,
                    [keyword = state->searchKeyword,
                     ownerUuid = state->ownerUuid,
                     page = (int)state->currentPage,
                     onBack = state->onBack](Player& target) {
                        showRecycleSearchForm(target, true, page, keyword, ownerUuid, onBack);
                    }
                );
                return;
            case ITEMS_CHEST_UI_RECORDS_SLOT:
                ChestUI::close(p);
                runForOnlinePlayerAfterTicks(
                    p,
                    ITEMS_CHEST_UI_OPEN_DELAY_TICKS,
                    [keyword = state->searchKeyword,
                     ownerUuid = state->ownerUuid,
                     page = state->currentPage,
                     onBack = state->onBack](Player& target) {
                        showTradeRecordMenuForm(target, [keyword, ownerUuid, page, onBack](Player& backPlayer) {
                            if (ownerUuid.empty()) {
                                showPublicRecycleItemsChestUi(backPlayer, page, keyword, onBack);
                            } else {
                                showPlayerPublicRecycleItemsChestUi(backPlayer, ownerUuid, page, keyword, onBack);
                            }
                        }, false);
                    }
                );
                return;
            case ITEMS_CHEST_UI_CLOSE_SLOT:
                ChestUI::close(p);
                return;
            case ITEMS_CHEST_UI_BACK_SLOT:
                if (state->onBack) {
                    state->onBack(p);
                }
                return;
            default:
                break;
            }

            if (ctx.slot == ITEMS_CHEST_UI_INFO_SLOT || ctx.slot >= ITEMS_CHEST_UI_PER_PAGE) {
                return;
            }

            auto currentData =
                buildPublicRecycleItemsChestUiPage(state->searchKeyword, state->currentPage, state->ownerUuid);
            if (ctx.slot >= currentData.entries.size()) {
                return;
            }

            const auto item = currentData.entries[ctx.slot];
            ChestUI::close(p);
            runForOnlinePlayerAfterTicks(
                p,
                ITEMS_CHEST_UI_OPEN_DELAY_TICKS,
                [item,
                 keyword = state->searchKeyword,
                 ownerUuid = state->ownerUuid,
                 page = state->currentPage,
                 onBack = state->onBack](Player& target) {
                    showRecycleItemDetailForm(target, item, [keyword, ownerUuid, page, onBack](Player& backPlayer) {
                        if (ownerUuid.empty()) {
                            showPublicRecycleItemsChestUi(backPlayer, page, keyword, onBack);
                        } else {
                            showPlayerPublicRecycleItemsChestUi(backPlayer, ownerUuid, page, keyword, onBack);
                        }
                    });
                }
            );
        };

        if (reopen || !ChestUI::isOpen(target)) {
            ChestUI::OpenRequest request;
            request.title        = pageData.title;
            request.items        = pageData.items;
            request.onClick      = std::move(handleClick);
            request.onClose      = [](Player&) {};
            request.closeOnClick = false;
            if (!ChestUI::open(target, std::move(request))) {
                showPublicRecycleItemsForm(target, static_cast<int>(state->currentPage), state->searchKeyword);
            }
            return;
        }

        ChestUI::UpdateRequest request;
        request.title        = pageData.title;
        request.items        = pageData.items;
        request.onClick      = std::move(handleClick);
        request.onClose      = [](Player&) {};
        request.closeOnClick = false;
        if (!ChestUI::update(target, std::move(request))) {
            (*refreshView)(target, true);
        }
    };

    (*refreshView)(player, false);
}

void showPlayerPublicRecycleItemsChestUi(
    Player&            player,
    const std::string& ownerUuid,
    size_t             currentPage,
    const std::string& searchKeyword,
    std::function<void(Player&)> onBack
) {
    struct PublicRecycleItemsChestUiState {
        std::string searchKeyword;
        std::string ownerUuid;
        std::function<void(Player&)> onBack;
        size_t      currentPage{0};
    };

    auto state = std::make_shared<PublicRecycleItemsChestUiState>(PublicRecycleItemsChestUiState{
        .searchKeyword = searchKeyword,
        .ownerUuid     = ownerUuid,
        .onBack        = std::move(onBack),
        .currentPage   = currentPage
    });

    auto refreshView = std::make_shared<std::function<void(Player&, bool)>>();
    *refreshView     = [state, refreshView](Player& target, bool reopen) {
        auto pageData = buildPublicRecycleItemsChestUiPage(state->searchKeyword, state->currentPage, state->ownerUuid);
        state->currentPage = pageData.currentPage;
        if (state->onBack) {
            pageData.items[ITEMS_CHEST_UI_BACK_SLOT] =
                makeChestUiControlItem("minecraft:arrow", I18nService::getInstance().get("form.button_back"));
        }

        auto handleClick = [state, refreshView](Player& p, ChestUI::ClickContext const& ctx) {
            switch (ctx.slot) {
            case ITEMS_CHEST_UI_PREV_SLOT:
                if (state->currentPage > 0) {
                    --state->currentPage;
                    (*refreshView)(p, false);
                }
                return;
            case ITEMS_CHEST_UI_NEXT_SLOT: {
                auto currentData = buildPublicRecycleItemsChestUiPage(
                    state->searchKeyword,
                    state->currentPage,
                    state->ownerUuid
                );
                if (state->currentPage + 1 < currentData.totalPages) {
                    ++state->currentPage;
                    (*refreshView)(p, false);
                }
                return;
            }
            case ITEMS_CHEST_UI_SEARCH_SLOT:
                ChestUI::close(p);
                runForOnlinePlayerAfterTicks(
                    p,
                    ITEMS_CHEST_UI_OPEN_DELAY_TICKS,
                    [keyword = state->searchKeyword,
                     ownerUuid = state->ownerUuid,
                     page = (int)state->currentPage,
                     onBack = state->onBack](Player& target) {
                        showRecycleSearchForm(target, true, page, keyword, ownerUuid, onBack);
                    }
                );
                return;
            case ITEMS_CHEST_UI_RECORDS_SLOT:
                ChestUI::close(p);
                runForOnlinePlayerAfterTicks(
                    p,
                    ITEMS_CHEST_UI_OPEN_DELAY_TICKS,
                    [keyword = state->searchKeyword,
                     ownerUuid = state->ownerUuid,
                     page = state->currentPage,
                     onBack = state->onBack](Player& target) {
                        showTradeRecordMenuForm(target, [keyword, ownerUuid, page, onBack](Player& backPlayer) {
                            showPlayerPublicRecycleItemsChestUi(backPlayer, ownerUuid, page, keyword, onBack);
                        }, false);
                    }
                );
                return;
            case ITEMS_CHEST_UI_CLOSE_SLOT:
                ChestUI::close(p);
                return;
            case ITEMS_CHEST_UI_BACK_SLOT:
                if (state->onBack) {
                    state->onBack(p);
                }
                return;
            default:
                break;
            }

            if (ctx.slot == ITEMS_CHEST_UI_INFO_SLOT || ctx.slot >= ITEMS_CHEST_UI_PER_PAGE) {
                return;
            }

            auto currentData =
                buildPublicRecycleItemsChestUiPage(state->searchKeyword, state->currentPage, state->ownerUuid);
            if (ctx.slot >= currentData.entries.size()) {
                return;
            }

            const auto item = currentData.entries[ctx.slot];
            ChestUI::close(p);
            runForOnlinePlayerAfterTicks(
                p,
                ITEMS_CHEST_UI_OPEN_DELAY_TICKS,
                [item,
                 keyword = state->searchKeyword,
                 ownerUuid = state->ownerUuid,
                 page = state->currentPage,
                 onBack = state->onBack](Player& target) {
                    showRecycleItemDetailForm(target, item, [keyword, ownerUuid, page, onBack](Player& backPlayer) {
                        showPlayerPublicRecycleItemsChestUi(backPlayer, ownerUuid, page, keyword, onBack);
                    });
                }
            );
        };

        if (reopen || !ChestUI::isOpen(target)) {
            ChestUI::OpenRequest request;
            request.title        = pageData.title;
            request.items        = pageData.items;
            request.onClick      = std::move(handleClick);
            request.onClose      = [](Player&) {};
            request.closeOnClick = false;
            if (!ChestUI::open(target, std::move(request))) {
                showPublicRecycleItemsForm(target, static_cast<int>(state->currentPage), state->searchKeyword);
            }
            return;
        }

        ChestUI::UpdateRequest request;
        request.title        = pageData.title;
        request.items        = pageData.items;
        request.onClick      = std::move(handleClick);
        request.onClose      = [](Player&) {};
        request.closeOnClick = false;
        if (!ChestUI::update(target, std::move(request))) {
            (*refreshView)(target, true);
        }
    };

    (*refreshView)(player, false);
}

static void showSearchForm(
    Player&            player,
    bool               returnToChestUi,
    int                currentPage,
    const std::string& currentKeyword,
    const std::string& ownerUuid,
    std::function<void(Player&)> onBack
) {
    auto&                i18n = I18nService::getInstance();
    ll::form::CustomForm fm;
    fm.setTitle(i18n.get("public_items.search_title"));
    fm.appendInput("keyword", i18n.get("public_items.search_keyword"), i18n.get("public_items.search_hint"), currentKeyword);

    fm.sendTo(player, [returnToChestUi, currentPage, currentKeyword, ownerUuid, onBack](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result.has_value() || result->empty()) {
            if (returnToChestUi) {
                if (ownerUuid.empty()) {
                    showPublicItemsChestUi(p, static_cast<size_t>(std::max(0, currentPage)), currentKeyword);
                } else {
                    showPlayerPublicItemsChestUi(
                        p,
                        ownerUuid,
                        static_cast<size_t>(std::max(0, currentPage)),
                        currentKeyword,
                        onBack
                    );
                }
            } else {
                showPublicItemsForm(p);
            }
            return;
        }

        std::string keyword;
        auto        keywordIt = result->find("keyword");
        if (keywordIt != result->end()) {
            if (auto* ptr = std::get_if<std::string>(&keywordIt->second)) {
                keyword = *ptr;
            }
        }
        if (returnToChestUi) {
            if (ownerUuid.empty()) {
                showPublicItemsChestUi(p, 0, keyword);
            } else {
                showPlayerPublicItemsChestUi(p, ownerUuid, 0, keyword, onBack);
            }
        } else {
            showPublicItemsForm(p, 0, keyword);
        }
    });
}

void showPublicRecycleItemsForm(Player& player, int currentPage, const std::string& searchKeyword) {
    auto& i18n = I18nService::getInstance();

    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get("public_items.recycle_list_title"));

    auto allItems = ShopRepository::getInstance().findAllPublicRecycleItems();

    std::vector<PublicRecycleItemData> filteredItems;
    std::string lowerKeyword = searchKeyword.empty() ? std::string{} : toLower(searchKeyword);
    for (const auto& item : allItems) {
        if (!lowerKeyword.empty()) {
            auto itemPtr = CT::FormUtils::createItemStackFromNbtString(item.itemNbt);
            if (!itemPtr) continue;
            std::string itemName = itemPtr->getName();
            std::string typeName = itemPtr->getTypeName();
            if (!fuzzyMatch(itemName, lowerKeyword) && !fuzzyMatch(typeName, lowerKeyword)) {
                continue;
            }
        }
        filteredItems.push_back(item);
    }

    int  totalItems = static_cast<int>(filteredItems.size());
    auto pageSlice  = Pagination::makeZeroBasedPageSlice(totalItems, ITEMS_PER_PAGE, currentPage);
    int  totalPages = pageSlice.totalPages;
    currentPage     = pageSlice.currentPage;
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

        int startIdx = pageSlice.startIndex;
        int endIdx   = pageSlice.endIndex;

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

    fm.appendButton(i18n.get("public_shop.button_close"), "textures/ui/cancel", "path", [](Player&) {});
    fm.sendTo(player);
}

static void showRecycleSearchForm(
    Player&            player,
    bool               returnToChestUi,
    int                currentPage,
    const std::string& currentKeyword,
    const std::string& ownerUuid,
    std::function<void(Player&)> onBack
) {
    auto&                i18n = I18nService::getInstance();
    ll::form::CustomForm fm;
    fm.setTitle(i18n.get("public_items.recycle_search_title"));
    fm.appendInput("keyword", i18n.get("public_items.search_keyword"), i18n.get("public_items.search_hint"), currentKeyword);

    fm.sendTo(player, [returnToChestUi, currentPage, currentKeyword, ownerUuid, onBack](Player& p, ll::form::CustomFormResult const& result, ll::form::FormCancelReason) {
        if (!result.has_value() || result->empty()) {
            if (returnToChestUi) {
                if (ownerUuid.empty()) {
                    showPublicRecycleItemsChestUi(p, static_cast<size_t>(std::max(0, currentPage)), currentKeyword);
                } else {
                    showPlayerPublicRecycleItemsChestUi(
                        p,
                        ownerUuid,
                        static_cast<size_t>(std::max(0, currentPage)),
                        currentKeyword,
                        onBack
                    );
                }
            } else {
                showPublicRecycleItemsForm(p);
            }
            return;
        }

        std::string keyword;
        auto        keywordIt = result->find("keyword");
        if (keywordIt != result->end()) {
            if (auto* ptr = std::get_if<std::string>(&keywordIt->second)) {
                keyword = *ptr;
            }
        }
        if (returnToChestUi) {
            if (ownerUuid.empty()) {
                showPublicRecycleItemsChestUi(p, 0, keyword);
            } else {
                showPlayerPublicRecycleItemsChestUi(p, ownerUuid, 0, keyword, onBack);
            }
        } else {
            showPublicRecycleItemsForm(p, 0, keyword);
        }
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
    bool               isRecycle,
    std::function<void(Player&)> onBack = {}
) {
    auto& i18n = I18nService::getInstance();

    auto itemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbt);
    if (!itemPtr) return;

    auto        ownerNameCache  = CT::FormUtils::getPlayerNameCache({ownerUuid});
    std::string ownerName       = ownerNameCache[ownerUuid];
    if (ownerName.empty()) {
        ownerName = i18n.get("public_shop.unknown_owner");
    }
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
        "public_shop.preview_owner",
        {
            {"owner", ownerName}
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
    content += buildDetailTeleportCostText();
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
    fm.appendButton(
        i18n.get("public_shop.button_back_list"),
        "textures/ui/arrow_left",
        "path",
        [isRecycle, onBack](Player& p) {
            if (onBack) {
                onBack(p);
                return;
            }
            isRecycle ? showPublicRecycleItemsForm(p) : showPublicItemsForm(p);
        }
    );
    fm.appendButton(i18n.get("public_shop.button_close"), "textures/ui/cancel", "path", [](Player&) {});
    fm.sendTo(player);
}

static void showShopItemDetailForm(Player& player, const PublicShopItemData& item, std::function<void(Player&)> onBack) {
    showItemDetailFormImpl(
        player,
        item.itemNbt,
        item.ownerUuid,
        item.shopName,
        item.price,
        item.pos,
        item.dimId,
        item.isOfficial,
        false,
        std::move(onBack)
    );
}

static void showRecycleItemDetailForm(
    Player&                      player,
    const PublicRecycleItemData& item,
    std::function<void(Player&)> onBack
) {
    auto& i18n = I18nService::getInstance();

    auto itemPtr = CT::FormUtils::createItemStackFromNbtString(item.itemNbt);
    if (!itemPtr) return;

    auto        ownerNameCache  = CT::FormUtils::getPlayerNameCache({item.ownerUuid});
    std::string ownerName       = ownerNameCache[item.ownerUuid];
    if (ownerName.empty()) {
        ownerName = i18n.get("public_shop.unknown_owner");
    }
    std::string shopDisplayName = item.shopName.empty()
        ? i18n.get("public_shop.owner_recycle_shop", {{"owner", ownerName}})
        : item.shopName;

    ll::form::SimpleForm fm;
    fm.setTitle(i18n.get("public_items.recycle_detail_title"));

    std::string content  = CT::FormUtils::getItemDisplayString(*itemPtr, 0, true) + "\n\n";
    content             += i18n.get("public_items.recycle_price", {{"price", CT::MoneyFormat::format(item.price)}});
    content             += buildRecycleRemainingText(item);
    content             += i18n.get("public_shop.preview_owner", {{"owner", ownerName}});
    content             += i18n.get("public_items.item_shop", {{"shop", shopDisplayName}});
    content             += i18n.get(
        "public_items.item_location",
        {
            {"dim", CT::FormUtils::dimIdToString(item.dimId)},
            {"x",   std::to_string(item.pos.x)             },
            {"y",   std::to_string(item.pos.y)             },
            {"z",   std::to_string(item.pos.z)             }
        }
    );
    content += buildDetailTeleportCostText();
    if (item.isOfficial) {
        content += i18n.get("public_items.item_official");
    }
    content += "\n" + i18n.get("public_shop.preview_recycle_notice");
    fm.setContent(content);

    fm.appendButton(
        i18n.get("public_shop.button_teleport"),
        "textures/ui/flyingascend_pressed",
        "path",
        [pos = item.pos, dimId = item.dimId](Player& p) {
            if (CT::FormUtils::teleportToShop(p, pos, dimId)) {
                p.sendMessage(I18nService::getInstance().get("public_shop.teleport_recycle_hint"));
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
                return;
            }
            showPublicRecycleItemsForm(p);
        }
    );
    fm.appendButton(i18n.get("public_shop.button_close"), "textures/ui/cancel", "path", [](Player&) {});
    fm.sendTo(player);
}

} // namespace CT
