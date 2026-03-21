#include "ShopForm.h"
#include "Config/ConfigManager.h"
#include "chestui/chestui.h"
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
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/PlayerInfo.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "mc/platform/UUID.h"
#include "mc/world/Container.h"
#include "mc/world/item/enchanting/EnchantmentInstance.h"
#include "mc/world/item/enchanting/ItemEnchants.h"
#include "mc/world/level/Level.h"
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
#include <chrono>
#include <memory>
#include <optional>
#include <string_view>

namespace CT {

namespace {

constexpr size_t kManageButtonLineWidth = 22;
constexpr size_t kShopChestUiPageSize   = 21;
constexpr size_t kShopChestUiPrevSlot   = 21;
constexpr size_t kShopChestUiInfoSlot   = 22;
constexpr size_t kShopChestUiNextSlot   = 23;
constexpr size_t kShopChestUiSearchSlot = 24;
constexpr size_t kShopChestUiRefreshSlot = 25;
constexpr size_t kShopChestUiCloseSlot  = 26;

struct ShopChestUiEntry {
    std::string itemNbtStr;
    double      unitPrice{0.0};
};

struct ShopChestUiPageData {
    std::string                  title;
    std::vector<ItemStack>       items;
    std::vector<ShopChestUiEntry> entries;
    size_t                       currentPage{0};
    size_t                       totalPages{1};
};

struct ManageShopItemEntry {
    ItemStack   item;
    int         totalCount;
    std::string priceStr;
    bool        soldOut{false};
};

std::string escapeSnbtString(const std::string& value) {
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

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trimCopy(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) != 0; });
    auto end =
        std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
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

size_t getUtf8CodePointLength(unsigned char leadByte) {
    if ((leadByte & 0x80u) == 0) {
        return 1;
    }
    if ((leadByte & 0xE0u) == 0xC0u) {
        return 2;
    }
    if ((leadByte & 0xF0u) == 0xE0u) {
        return 3;
    }
    if ((leadByte & 0xF8u) == 0xF0u) {
        return 4;
    }
    return 1;
}

size_t getDisplayWidthForCodePoint(unsigned char leadByte) { return (leadByte & 0x80u) == 0 ? 1 : 2; }

std::string stripMinecraftFormatting(const std::string& text) {
    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size();) {
        if (i + 2 < text.size() && static_cast<unsigned char>(text[i]) == 0xC2u
            && static_cast<unsigned char>(text[i + 1]) == 0xA7u) {
            i += 3;
            continue;
        }

        auto cpLen = std::min(getUtf8CodePointLength(static_cast<unsigned char>(text[i])), text.size() - i);
        result.append(text, i, cpLen);
        i += cpLen;
    }

    return result;
}

std::string normalizeManagePriceText(const std::string& priceText) {
    std::string normalized = stripMinecraftFormatting(priceText);

    auto replaceAll = [](std::string& target, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = target.find(from, pos)) != std::string::npos) {
            target.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    replaceAll(normalized, "[", "");
    replaceAll(normalized, "]", "");
    replaceAll(normalized, "已定价", "价格");
    replaceAll(normalized, "Priced", "Price");
    return trimCopy(normalized);
}

std::string truncateDisplayText(const std::string& text, size_t maxWidth) {
    if (maxWidth == 0) {
        return {};
    }

    constexpr size_t ellipsisWidth = 3;

    size_t visibleWidth = 0;
    size_t index        = 0;
    bool   truncated    = false;

    while (index < text.size()) {
        auto cpLen   = std::min(getUtf8CodePointLength(static_cast<unsigned char>(text[index])), text.size() - index);
        auto cpWidth = getDisplayWidthForCodePoint(static_cast<unsigned char>(text[index]));
        if (visibleWidth + cpWidth > maxWidth) {
            truncated = true;
            break;
        }
        visibleWidth += cpWidth;
        index        += cpLen;
    }

    if (!truncated) {
        return text;
    }

    if (maxWidth <= ellipsisWidth) {
        return std::string(maxWidth, '.');
    }

    size_t targetWidth   = maxWidth - ellipsisWidth;
    size_t trimmedWidth  = 0;
    size_t trimmedLength = 0;
    while (trimmedLength < text.size()) {
        auto cpLen = std::min(
            getUtf8CodePointLength(static_cast<unsigned char>(text[trimmedLength])),
            text.size() - trimmedLength
        );
        auto cpWidth = getDisplayWidthForCodePoint(static_cast<unsigned char>(text[trimmedLength]));
        if (trimmedWidth + cpWidth > targetWidth) {
            break;
        }
        trimmedWidth  += cpWidth;
        trimmedLength += cpLen;
    }

    return text.substr(0, trimmedLength) + "...";
}

std::optional<std::string> getCompactEnchantSummary(const ItemStack& item) {
    if (item.isEnchanted()) {
        ItemEnchants enchants    = item.constructItemEnchantsFromUserData();
        auto         enchantList = enchants.getAllEnchants();
        if (!enchantList.empty()) {
            std::string summary = CT::NbtUtils::enchantToString(enchantList.front().mEnchantType) + " "
                                + std::to_string(enchantList.front().mLevel);
            if (enchantList.size() > 1) {
                summary += " +" + std::to_string(enchantList.size() - 1);
            }
            return summary;
        }
    }

    return std::nullopt;
}

std::optional<std::string> getCompactAuxSummary(const ItemStack& item) {
    short auxValue = item.getAuxValue();
    if (auxValue != 0) {
        return "Aux " + std::to_string(auxValue);
    }

    return std::nullopt;
}

std::string buildManageButtonText(const ItemStack& item, int totalCount, const std::string& priceStr) {
    std::string nameLine = stripMinecraftFormatting(std::string(item.getName()));
    if (nameLine.empty()) {
        nameLine = item.getTypeName();
    }
    if (totalCount > 0) {
        nameLine += " x" + std::to_string(totalCount);
    }

    std::string detailLine = normalizeManagePriceText(priceStr);
    if (auto enchantSummary = getCompactEnchantSummary(item); enchantSummary.has_value()) {
        detailLine = detailLine.empty() ? *enchantSummary : detailLine + " " + *enchantSummary;
    }
    if (auto auxSummary = getCompactAuxSummary(item); auxSummary.has_value()) {
        detailLine = detailLine.empty() ? *auxSummary : detailLine + " " + *auxSummary;
    }

    return truncateDisplayText(nameLine, kManageButtonLineWidth) + "\n"
         + truncateDisplayText(detailLine, kManageButtonLineWidth);
}

std::string buildManagePriceLabel(TextService& txt, std::optional<double> price, bool soldOut) {
    std::string label = price.has_value() ? txt.getMessage(
                                                "form.label_priced",
                                                {
                                                    {"price", CT::MoneyFormat::format(*price)}
    }
                                            )
                                          : txt.getMessage("form.label_not_priced");
    if (soldOut) {
        label += " " + txt.getMessage("form.label_sold_out");
    }
    return label;
}

std::unique_ptr<ItemStack> createDisplayItem(const std::string& itemNbtStr) {
    auto itemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbtStr);
    if (itemPtr) {
        itemPtr->set(1);
    }
    return itemPtr;
}

std::unique_ptr<CompoundTag> buildChestUiDisplayTag(const std::string& name, const std::vector<std::string>& loreLines) {
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

bool applyChestUiDisplayTag(ItemStack& item, const std::string& name, const std::vector<std::string>& loreLines) {
    auto itemNbt = NbtUtils::getItemNbt(item);
    if (!itemNbt) {
        return false;
    }

    CompoundTag tagNbt;
    if (itemNbt->contains("tag")) {
        tagNbt = itemNbt->at("tag").get<CompoundTag>();
    }

    auto displayTag = buildChestUiDisplayTag(name, loreLines);
    if (!displayTag) {
        return false;
    }

    tagNbt["display"] = *displayTag;
    (*itemNbt)["tag"] = std::move(tagNbt);
    return NbtUtils::setItemNbt(item, *itemNbt);
}

Player* findOnlinePlayer(const std::string& uuidString) {
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

void runAfterTicks(int ticks, std::function<void()> task) {
    if (ticks <= 0) {
        ll::thread::ServerThreadExecutor::getDefault().execute(std::move(task));
        return;
    }

    ll::thread::ServerThreadExecutor::getDefault().executeAfter(std::move(task), std::chrono::milliseconds(ticks * 50));
}

void runForOnlinePlayerAfterTicks(Player& player, int ticks, std::function<void(Player&)> task) {
    auto playerUuid = player.getUuid().asString();
    runAfterTicks(ticks, [playerUuid, task = std::move(task)]() mutable {
        if (auto* target = findOnlinePlayer(playerUuid)) {
            task(*target);
        }
    });
}

ItemStack makeChestUiControlItem(
    std::string_view                typeName,
    const std::string&              displayName,
    const std::vector<std::string>& loreLines = {},
    int                             count     = 1
) {
    ItemStack item;
    item.reinit(typeName, count, 0);
    applyChestUiDisplayTag(item, displayName, loreLines);
    return item;
}

void decorateShopChestUiItem(ItemStack& item, double unitPrice, int stock) {
    auto&       txt      = TextService::getInstance();
    std::string itemName = stripMinecraftFormatting(std::string(item.getName()));
    if (itemName.empty()) {
        itemName = item.getTypeName();
    }

    std::vector<std::string> loreLines{
        txt.getMessage("form.chest_ui_item_price", {{"price", CT::MoneyFormat::format(unitPrice)}}),
        txt.getMessage("form.chest_ui_item_stock", {{"stock", std::to_string(std::max(stock, 0))}}),
        txt.getMessage("form.chest_ui_item_hint")
    };

    applyChestUiDisplayTag(item, "§r" + itemName, loreLines);
}

int getChestUiDisplayCount(int chestStock, int dbStock) {
    int preferred = chestStock > 0 ? chestStock : dbStock;
    if (preferred <= 0) {
        return 1;
    }
    return std::min(preferred, 64);
}

std::string buildShopChestUiTitle(BlockPos pos, int dimId, BlockSource& region, const std::string& searchKeyword, size_t page, size_t totalPages) {
    auto& txt   = TextService::getInstance();
    auto  info  = ChestService::getInstance().getChestInfo(pos, dimId, region);
    auto  title = txt.getMessage("form.shop_items_title");
    if (info && !info->ownerUuid.empty()) {
        auto        ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(info->ownerUuid));
        std::string ownerName = ownerInfo ? ownerInfo->name : txt.getMessage("public_shop.unknown_owner");
        title                 = txt.getMessage("form.shop_items_title_with_owner", {{"owner", ownerName}});
    }

    title += " §7(" + std::to_string(page + 1) + "/" + std::to_string(totalPages) + ")§r";
    if (!searchKeyword.empty()) {
        title += " §6" + searchKeyword + "§r";
    }
    return title;
}

ShopChestUiPageData buildShopChestUiPage(
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    const std::string& searchKeyword,
    size_t             requestedPage
) {
    std::vector<ShopChestUiEntry> matchedEntries;
    matchedEntries.reserve(kShopChestUiPageSize);

    auto& txt  = TextService::getInstance();
    auto items = ShopService::getInstance().getShopItems(pos, dimId, region);

    struct MatchedItemData {
        ShopChestUiEntry entry;
        ItemStack        displayItem;
    };

    std::vector<MatchedItemData> matchedItems;
    matchedItems.reserve(items.size());

    for (const auto& shopItem : items) {
        std::string itemNbtStr = ItemRepository::getInstance().getItemNbt(shopItem.itemId);
        if (itemNbtStr.empty()) {
            continue;
        }

        auto itemPtr = CT::FormUtils::createItemStackFromNbtString(itemNbtStr);
        if (!itemPtr) {
            continue;
        }

        int actualStock =
            CT::FormUtils::tryCountItemsInChest(region, pos, dimId, itemNbtStr).value_or(shopItem.dbCount);
        ItemStack displayItem = *itemPtr;
        displayItem.set(getChestUiDisplayCount(actualStock, shopItem.dbCount));
        decorateShopChestUiItem(displayItem, shopItem.price, actualStock);

        if (!itemMatchesKeyword(displayItem, searchKeyword)) {
            continue;
        }

        matchedItems.push_back(MatchedItemData{
            .entry       = ShopChestUiEntry{.itemNbtStr = std::move(itemNbtStr), .unitPrice = shopItem.price},
            .displayItem = std::move(displayItem)
        });
    }

    size_t totalPages = std::max<size_t>(1, (matchedItems.size() + kShopChestUiPageSize - 1) / kShopChestUiPageSize);
    size_t currentPage = std::min(requestedPage, totalPages - 1);
    size_t startIndex  = currentPage * kShopChestUiPageSize;
    size_t endIndex    = std::min(startIndex + kShopChestUiPageSize, matchedItems.size());

    std::vector<ItemStack> chestItems(27, ItemStack::EMPTY_ITEM());
    matchedEntries.reserve(endIndex > startIndex ? endIndex - startIndex : 0);

    for (size_t index = startIndex; index < endIndex; ++index) {
        chestItems[index - startIndex] = matchedItems[index].displayItem;
        matchedEntries.push_back(matchedItems[index].entry);
    }

    if (currentPage > 0) {
        chestItems[kShopChestUiPrevSlot] = makeChestUiControlItem(
            "minecraft:arrow",
            txt.getMessage("public_shop.button_prev_page")
        );
    }
    chestItems[kShopChestUiInfoSlot]    = makeChestUiControlItem(
        "minecraft:book",
        txt.getMessage(
            "form.chest_ui_page_info",
            {{"page", std::to_string(currentPage + 1)}, {"total", std::to_string(totalPages)}}
        ),
        {},
        static_cast<int>(std::min<size_t>(totalPages, 64))
    );
    if (currentPage + 1 < totalPages) {
        chestItems[kShopChestUiNextSlot] = makeChestUiControlItem(
            "minecraft:arrow",
            txt.getMessage("public_shop.button_next_page")
        );
    }
    chestItems[kShopChestUiSearchSlot]  = makeChestUiControlItem(
        "minecraft:compass",
        txt.getMessage("public_shop.button_search")
    );
    chestItems[kShopChestUiRefreshSlot] = makeChestUiControlItem(
        "minecraft:clock",
        txt.getMessage("form.chest_ui_refresh")
    );
    chestItems[kShopChestUiCloseSlot]   = makeChestUiControlItem(
        "minecraft:barrier",
        txt.getMessage("public_shop.button_close")
    );

    return ShopChestUiPageData{
        .title       = buildShopChestUiTitle(pos, dimId, region, searchKeyword, currentPage, totalPages),
        .items       = std::move(chestItems),
        .entries     = std::move(matchedEntries),
        .currentPage = currentPage,
        .totalPages  = totalPages
    };
}

void showShopItemSearchForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    const std::string& currentKeyword,
    bool               returnToChestUi,
    size_t             returnPage
) {
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
        [pos, dimId, currentKeyword, returnToChestUi, returnPage](
            Player& p,
            const ll::form::CustomFormResult& result,
            ll::form::FormCancelReason
        ) {
            auto& region = p.getDimensionBlockSource();
            if (!result.has_value()) {
                if (returnToChestUi) {
                    showShopChestItemsUi(p, pos, dimId, region, currentKeyword, returnPage);
                } else {
                    showShopChestItemsForm(p, pos, dimId, region, currentKeyword);
                }
                return;
            }

            const auto  keywordIt = result->find("keyword");
            std::string keyword;
            if (keywordIt != result->end()) {
                if (const auto* value = std::get_if<std::string>(&keywordIt->second)) {
                    keyword = trimCopy(*value);
                }
            }
            if (returnToChestUi) {
                showShopChestItemsUi(p, pos, dimId, region, keyword, 0);
            } else {
                showShopChestItemsForm(p, pos, dimId, region, keyword);
            }
        }
    );
}

} // namespace

void showShopChestItemsUi(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    const std::string& searchKeyword,
    size_t             page
) {
    (void)region;

    struct ShopChestUiState {
        BlockPos    pos;
        int         dimId{0};
        std::string searchKeyword;
        size_t      currentPage{0};
    };

    auto state = std::make_shared<ShopChestUiState>(ShopChestUiState{
        .pos           = pos,
        .dimId         = dimId,
        .searchKeyword = searchKeyword,
        .currentPage   = page
    });

    auto refreshView = std::make_shared<std::function<void(Player&, bool)>>();
    *refreshView     = [state, refreshView](Player& target, bool reopen) {
        auto& regionRef = target.getDimensionBlockSource();
        auto  pageData  = buildShopChestUiPage(state->pos, state->dimId, regionRef, state->searchKeyword, state->currentPage);
        state->currentPage = pageData.currentPage;

        auto handleClick = [state, refreshView](Player& p, ChestUI::ClickContext const& ctx) {
            auto& regionRef = p.getDimensionBlockSource();

            if (ctx.slot == kShopChestUiPrevSlot) {
                if (state->currentPage > 0) {
                    --state->currentPage;
                    (*refreshView)(p, false);
                }
                return;
            }

            if (ctx.slot == kShopChestUiNextSlot) {
                auto currentData = buildShopChestUiPage(
                    state->pos,
                    state->dimId,
                    regionRef,
                    state->searchKeyword,
                    state->currentPage
                );
                if (state->currentPage + 1 < currentData.totalPages) {
                    ++state->currentPage;
                    (*refreshView)(p, false);
                }
                return;
            }

            if (ctx.slot == kShopChestUiSearchSlot) {
                ChestUI::close(p);
                runForOnlinePlayerAfterTicks(
                    p,
                    2,
                    [pos = state->pos, dimId = state->dimId, keyword = state->searchKeyword, page = state->currentPage](
                        Player& target
                    ) { showShopItemSearchForm(target, pos, dimId, keyword, true, page); }
                );
                return;
            }

            if (ctx.slot == kShopChestUiRefreshSlot) {
                (*refreshView)(p, false);
                return;
            }

            if (ctx.slot == kShopChestUiCloseSlot) {
                ChestUI::close(p);
                return;
            }

            if (ctx.slot == kShopChestUiInfoSlot || ctx.slot >= kShopChestUiPageSize) {
                return;
            }

            auto currentData = buildShopChestUiPage(
                state->pos,
                state->dimId,
                regionRef,
                state->searchKeyword,
                state->currentPage
            );
            if (ctx.slot >= currentData.entries.size()) {
                return;
            }

            const auto& entry = currentData.entries[ctx.slot];
            ChestUI::close(p);
            runForOnlinePlayerAfterTicks(
                p,
                2,
                [pos          = state->pos,
                 dimId        = state->dimId,
                 itemNbtStr   = entry.itemNbtStr,
                 unitPrice    = entry.unitPrice,
                 keyword      = state->searchKeyword,
                 currentPage  = state->currentPage](Player& target) {
                    auto& regionLater = target.getDimensionBlockSource();
                    showShopItemBuyForm(
                        target,
                        pos,
                        dimId,
                        0,
                        unitPrice,
                        regionLater,
                        itemNbtStr,
                        keyword,
                        true,
                        currentPage
                    );
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
                showShopChestItemsForm(target, state->pos, state->dimId, regionRef, state->searchKeyword);
            }
            return;
        }

        ChestUI::UpdateRequest request;
        request.title = pageData.title;
        request.items = pageData.items;
        if (!ChestUI::update(target, std::move(request))) {
            (*refreshView)(target, true);
        }
    };

    (*refreshView)(player, true);
}

void showShopChestItemsForm(
    Player&            player,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region,
    const std::string& searchKeyword
) {
    ll::form::SimpleForm fm;
    auto&                txt = TextService::getInstance();

    std::string title = txt.getMessage("form.shop_items_title");
    auto        info  = ChestService::getInstance().getChestInfo(pos, dimId, region);
    if (info && !info->ownerUuid.empty()) {
        auto        ownerInfo = ll::service::PlayerInfo::getInstance().fromUuid(mce::UUID::fromString(info->ownerUuid));
        std::string ownerName = ownerInfo ? ownerInfo->name : txt.getMessage("public_shop.unknown_owner");
        title                 = txt.getMessage(
            "form.shop_items_title_with_owner",
            {
                {"owner", ownerName}
        }
        );
    }
    fm.setTitle(title);

    logger.debug(
        "showShopChestItemsForm: 玩家 {} 正在打开位于 ({},{},{})、维度 {} 的商店。",
        player.getRealName(),
        pos.x,
        pos.y,
        pos.z,
        dimId
    );

    auto items            = ShopService::getInstance().getShopItems(pos, dimId, region);
    int  matchedItemCount = 0;

    fm.appendButton(
        txt.getMessage("public_shop.button_search"),
        "textures/ui/magnifyingGlass",
        "path",
        [pos, dimId, searchKeyword](Player& p) { showShopItemSearchForm(p, pos, dimId, searchKeyword, false, 0); }
    );

    if (items.empty()) {
        fm.setContent(txt.getMessage("shop.empty"));
        logger.debug("showShopChestItemsForm: 位于 ({},{},{})、维度 {} 的商店为空。", pos.x, pos.y, pos.z, dimId);
    } else {
        logger.debug("showShopChestItemsForm: 商店中找到 {} 个商品。", items.size());
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

            int totalCount =
                CT::FormUtils::tryCountItemsInChest(region, pos, dimId, itemNbtStr).value_or(shopItem.dbCount);
            std::string buttonText =
                txt.generateShopItemText(item.getName(), shopItem.price, shopItem.dbCount, totalCount);
            std::string texturePath = CT::FormUtils::getItemTexturePath(item);

            if (!texturePath.empty()) {
                fm.appendButton(
                    buttonText,
                    texturePath,
                    "path",
                    [pos, dimId, itemNbtStr, unitPrice = shopItem.price, searchKeyword](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showShopItemBuyForm(p, pos, dimId, 0, unitPrice, region, itemNbtStr, searchKeyword);
                    }
                );
            } else {
                fm.appendButton(
                    buttonText,
                    [pos, dimId, itemNbtStr, unitPrice = shopItem.price, searchKeyword](Player& p) {
                        auto& region = p.getDimensionBlockSource();
                        showShopItemBuyForm(p, pos, dimId, 0, unitPrice, region, itemNbtStr, searchKeyword);
                    }
                );
            }
        }

        if (!searchKeyword.empty()) {
            std::string content = txt.getMessage(
                "public_items.search_result",
                {
                    {"keyword", searchKeyword}
            }
            );
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

void showShopItemPriceForm(
    Player&            player,
    const std::string& itemNbtStr,
    BlockPos           pos,
    int                dimId,
    BlockSource&       region
) {
    ll::form::CustomForm fm;
    auto&                txt = TextService::getInstance();
    auto                 itemPtr = createDisplayItem(itemNbtStr);
    if (!itemPtr) {
        player.sendMessage(txt.getMessage("shop.item_manage_fail"));
        showShopChestManageForm(player, pos, dimId, region);
        return;
    }
    const ItemStack& item = *itemPtr;
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
        [itemNbtStr, pos, dimId](
            Player&                           p,
            const ll::form::CustomFormResult& result,
            ll::form::FormCancelReason
        ) {
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
                int         dbCount    = ShopService::getInstance().countItemsInChest(region, pos, dimId, itemNbtStr);

                auto priceResult =
                    ShopService::getInstance().setItemPrice(
                        pos,
                        dimId,
                        itemNbtStr,
                        price,
                        dbCount,
                        region,
                        p.getUuid().asString()
                    );
                p.sendMessage(priceResult.message);
            } catch (const std::exception& e) {
                p.sendMessage(txt.getMessage("input.invalid_number"));
                logger.debug("showShopItemPriceForm: 设置物品价格时发生错误: {}", e.what());
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
    BlockPos  mainPos = ChestService::getInstance().getMainChestPos(pos, region);

    int totalCount = ShopService::getInstance().countItemsInChest(region, pos, dimId, itemNbtStr);
    int itemId     = ItemRepository::getInstance().getOrCreateItemId(itemNbtStr);
    if (itemId < 0) {
        player.sendMessage(txt.getMessage("shop.item_id_fail"));
        showShopChestManageForm(player, pos, dimId, region);
        return;
    }

    auto itemOpt = ShopRepository::getInstance().findItem(mainPos, dimId, itemId);

    std::string content = txt.getMessage(
                              "form.label_managing_item",
                              {
                                  {"item", std::string(item.getName())}
    }
                          )
                        + "\n";
    content += CT::FormUtils::getItemDisplayString(item, 0, true) + "\n\n";
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

    fm.appendButton(txt.getMessage("form.button_set_price"), [itemNbtStr, pos, dimId](Player& p) {
        auto& region = p.getDimensionBlockSource();
        showShopItemPriceForm(p, itemNbtStr, pos, dimId, region);
    });

    fm.appendButton(
        txt.getMessage("form.button_item_limit"),
        [pos, dimId, itemId, itemName = std::string(item.getName())](Player& p) {
            auto& region = p.getDimensionBlockSource();
            showPlayerItemLimitForm(p, pos, dimId, region, true, itemId, itemName);
        }
    );

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
    BlockPos             mainPos = ChestService::getInstance().getMainChestPos(pos, region);
    fm.setTitle(txt.getMessage("form.shop_manage_title"));

    logger.debug(
        "showShopChestManageForm: 玩家 {} 正在管理位于 ({},{},{})、维度 {} 的商店。",
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
            "showShopChestManageForm: 无法获取箱子实体或类型不匹配在 ({}, {}, {}) dim {}",
            pos.x,
            pos.y,
            pos.z,
            dimId
        );
        return;
    }

    auto* chest     = static_cast<ChestBlockActor*>(blockActor);
    if (chest->mLargeChestPaired && !chest->mPairLead && chest->mLargeChestPaired) {
        chest = chest->mLargeChestPaired;
    }
    auto* container = chest->getContainer();
    if (!container) {
        player.sendMessage(txt.getMessage("chest.entity_fail"));
        logger.error(
            "showShopChestManageForm: 无法获取箱子容器在 ({}, {}, {}) dim {}",
            pos.x,
            pos.y,
            pos.z,
            dimId
        );
        return;
    }

    bool                                         isEmpty = true;
    std::map<std::string, ManageShopItemEntry>   aggregatedItems;

    for (int i = 0; i < container->getContainerSize(); ++i) {
        const auto& itemInSlot = container->getItem(i);
        if (!itemInSlot.isNull()) {
            isEmpty = false;
            logger.debug(
                "showShopChestManageForm: 正在处理槽位 {} 中的物品 '{}'，数量 {}。",
                i,
                itemInSlot.getName(),
                itemInSlot.mCount
            );
            auto itemNbt = CT::NbtUtils::getItemNbt(itemInSlot);
            if (!itemNbt) {
                logger.error("showShopChestManageForm: 无法获取槽位 {} 的 NBT 数据。", i);
                continue;
            }

            std::string itemNbtStr =
                CT::NbtUtils::toSNBT(*CT::NbtUtils::cleanNbtForComparison(*itemNbt, itemInSlot.isDamageableItem()));
            logger.debug("showShopChestManageForm: 槽位 {} 的物品比较用 NBT: {}", i, itemNbtStr);

            if (aggregatedItems.count(itemNbtStr)) {
                aggregatedItems[itemNbtStr].totalCount += itemInSlot.mCount;
                logger.debug(
                    "showShopChestManageForm: 物品 '{}' 已聚合，更新后的总数量为 {}。",
                    itemInSlot.getName(),
                    aggregatedItems[itemNbtStr].totalCount
                );
            } else {
                int         itemId   = ItemRepository::getInstance().getOrCreateItemId(itemNbtStr);
                bool        soldOut  = false;
                std::string priceStr = buildManagePriceLabel(txt, std::nullopt, false);
                if (itemId > 0) {
                    auto itemOpt = ShopRepository::getInstance().findItem(mainPos, dimId, itemId);
                    if (itemOpt) {
                        soldOut  = itemOpt->dbCount <= 0;
                        priceStr = buildManagePriceLabel(txt, itemOpt->price, soldOut);
                    }
                }
                aggregatedItems[itemNbtStr] = ManageShopItemEntry{itemInSlot, (int)itemInSlot.mCount, priceStr, soldOut};
            }
        } else {
            logger.debug("showShopChestManageForm: 槽位 {} 为空。", i);
        }
    }
    logger.debug(
        "showShopChestManageForm: 物品聚合完成，共发现 {} 种唯一物品。",
        aggregatedItems.size()
    );

    auto listedItems = ShopService::getInstance().getShopItems(pos, dimId, region);
    for (const auto& listedItem : listedItems) {
        std::string priceStr = buildManagePriceLabel(txt, listedItem.price, listedItem.dbCount <= 0);

        auto existing = aggregatedItems.find(listedItem.itemNbt);
        if (existing != aggregatedItems.end()) {
            existing->second.priceStr = priceStr;
            existing->second.soldOut  = listedItem.dbCount <= 0;
            continue;
        }

        auto itemPtr = CT::FormUtils::createItemStackFromNbtString(listedItem.itemNbt);
        if (!itemPtr) {
            logger.error(
                "showShopChestManageForm: 无法从已上架物品 NBT 创建物品，itemId={}，位置 ({},{},{})，维度 {}。",
                listedItem.itemId,
                pos.x,
                pos.y,
                pos.z,
                dimId
            );
            continue;
        }

        aggregatedItems.emplace(
            listedItem.itemNbt,
            ManageShopItemEntry{*itemPtr, listedItem.dbCount, priceStr, listedItem.dbCount <= 0}
        );
    }

    std::vector<std::pair<std::string, ManageShopItemEntry const*>> orderedItems;
    orderedItems.reserve(aggregatedItems.size());
    for (const auto& [itemNbtStr, itemEntry] : aggregatedItems) {
        orderedItems.emplace_back(itemNbtStr, &itemEntry);
    }

    std::stable_sort(
        orderedItems.begin(),
        orderedItems.end(),
        [](const auto& left, const auto& right) { return !left.second->soldOut && right.second->soldOut; }
    );

    for (const auto& [itemNbtStr, itemEntry] : orderedItems) {
        const ItemStack&   item       = itemEntry->item;
        int                totalCount = itemEntry->totalCount;
        const std::string& priceStr   = itemEntry->priceStr;

        std::string buttonText  = buildManageButtonText(item, totalCount, priceStr);
        std::string texturePath = CT::FormUtils::getItemTexturePath(item);

        logger.debug(
            "showShopChestManageForm: 正在为物品 '{}' 添加按钮，按钮文本 '{}'，贴图路径 '{}'。",
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

    if (isEmpty && aggregatedItems.empty()) {
        fm.setContent(txt.getMessage("shop.chest_empty"));
        logger.debug("showShopChestManageForm: 位于 ({},{},{})、维度 {} 的箱子为空。", pos.x, pos.y, pos.z, dimId);
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
    BlockPos           pos,
    int                dimId,
    int                slot,
    double             unitPrice, // 修改为 double
    BlockSource&       region,
    const std::string& itemNbtStr, // 添加 itemNbtStr 参数
    const std::string& searchKeyword,
    bool               returnToChestUi,
    size_t             returnPage
) {
    (void)region;
    ll::form::CustomForm fm;
    auto&                txt = TextService::getInstance();
    auto reopenBrowse = [pos, dimId, searchKeyword, returnToChestUi, returnPage](Player& target) {
        auto& regionRef = target.getDimensionBlockSource();
        if (returnToChestUi) {
            showShopChestItemsUi(target, pos, dimId, regionRef, searchKeyword, returnPage);
        } else {
            showShopChestItemsForm(target, pos, dimId, regionRef, searchKeyword);
        }
    };
    auto                 itemPtr = createDisplayItem(itemNbtStr);
    if (!itemPtr) {
        player.sendMessage(txt.getMessage("shop.data_corrupt"));
        reopenBrowse(player);
        return;
    }
    const ItemStack& item = *itemPtr;
    fm.setTitle(txt.getMessage("form.shop_buy_title"));

    logger.debug(
        "showShopItemBuyForm: 玩家 {} 正在查看位于 ({},{},{})、维度 {} 的物品 {}，单价 {}，itemNbtStr 为 {}。",
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
        [pos, dimId, slot, unitPrice, itemNbtStr, searchKeyword, returnToChestUi, returnPage, reopenBrowse](
            Player&                           p,
            const ll::form::CustomFormResult& result,
            ll::form::FormCancelReason
        ) {
            auto& region = p.getDimensionBlockSource();
            auto& txt    = TextService::getInstance();
            if (!result.has_value()) {
                logger.debug("showShopItemBuyForm: 玩家 {} 取消了购买。", p.getRealName());
                if (!returnToChestUi) {
                    p.sendMessage(txt.getMessage("action.cancelled"));
                }
                reopenBrowse(p);
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
                logger.debug("showShopItemBuyForm: 玩家 {} 输入的购买数量为 {}。", p.getRealName(), buyCount);
                if (buyCount <= 0) {
                    p.sendMessage(txt.getMessage("input.invalid_count"));
                    logger.warn("showShopItemBuyForm: 玩家 {} 输入了无效的购买数量 {}。", p.getRealName(), buyCount);
                    showShopItemBuyForm(
                        p,
                        pos,
                        dimId,
                        slot,
                        unitPrice,
                        region,
                        itemNbtStr,
                        searchKeyword,
                        returnToChestUi,
                        returnPage
                    );
                    return;
                }
            } catch (const std::exception& e) {
                p.sendMessage(txt.getMessage("input.invalid_buy_count"));
                logger.error("showShopItemBuyForm: 解析玩家 {} 的购买数量时出错: {}", p.getRealName(), e.what());
                showShopItemBuyForm(
                    p,
                    pos,
                    dimId,
                    slot,
                    unitPrice,
                    region,
                    itemNbtStr,
                    searchKeyword,
                    returnToChestUi,
                    returnPage
                );
                return;
            }

            int itemId = ItemRepository::getInstance().getOrCreateItemId(itemNbtStr);
            if (itemId < 0) {
                p.sendMessage(txt.getMessage("shop.purchase_id_fail"));
                logger.error("showShopItemBuyForm: 无法为物品 NBT {} 获取或创建 item_id。", itemNbtStr);
                reopenBrowse(p);
                return;
            }

            auto purchaseResult =
                ShopService::getInstance().purchaseItem(p, pos, dimId, itemId, buyCount, region, itemNbtStr);
            p.sendMessage(purchaseResult.message);
            reopenBrowse(p);
        }
    );
}

void showSetShopNameForm(Player& player, BlockPos pos, int dimId, BlockSource& region) {
    (void)region;
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
