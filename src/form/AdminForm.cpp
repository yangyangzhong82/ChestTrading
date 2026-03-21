#include "AdminForm.h"
#include "FormUtils.h"
#include "Utils/NbtUtils.h"
#include "Utils/Pagination.h"
#include "chestui/chestui.h"
#include "ll/api/form/CustomForm.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/service/PlayerInfo.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "mc/platform/UUID.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "repository/ChestRepository.h"
#include "service/I18nService.h"
#include "service/TextService.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>


namespace CT {

// 解析 "x,y,z" 格式的字符串
std::optional<BlockPos> parseCoordinates(const std::string& coordStr) {
    if (coordStr.empty()) {
        return std::nullopt;
    }
    std::stringstream ss(coordStr);
    std::string       item;
    std::vector<int>  coords;
    while (std::getline(ss, item, ',')) {
        try {
            coords.push_back(std::stoi(item));
        } catch (const std::exception&) {
            return std::nullopt; // 解析失败
        }
    }
    if (coords.size() == 3) {
        return BlockPos{coords[0], coords[1], coords[2]};
    }
    return std::nullopt;
}

namespace {

constexpr size_t kAdminChestUiPageSize    = 45;
constexpr size_t kAdminChestUiPrevSlot    = 45;
constexpr size_t kAdminChestUiInfoSlot    = 46;
constexpr size_t kAdminChestUiNextSlot    = 47;
constexpr size_t kAdminChestUiFilterSlot  = 48;
constexpr size_t kAdminChestUiBackSlot    = 49;
constexpr size_t kAdminChestUiCloseSlot   = 50;
constexpr int    kAdminChestUiDelayTicks  = 2;

std::string coordRangeToInput(const CoordinateRange& coordRange, bool useMin) {
    if (!coordRange.minX.has_value() || !coordRange.minY.has_value() || !coordRange.minZ.has_value()) {
        return {};
    }
    if (!useMin && (!coordRange.maxX.has_value() || !coordRange.maxY.has_value() || !coordRange.maxZ.has_value())) {
        return {};
    }

    int x = useMin ? *coordRange.minX : *coordRange.maxX;
    int y = useMin ? *coordRange.minY : *coordRange.maxY;
    int z = useMin ? *coordRange.minZ : *coordRange.maxZ;
    return std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
}

std::vector<ChestData> collectFilteredAdminChests(
    const std::vector<int>&       dimIdFilter,
    const std::vector<ChestType>& chestTypeFilter,
    const CoordinateRange&        coordRange
) {
    auto                   allChests = ChestRepository::getInstance().findAll();
    std::vector<ChestData> filteredChests;

    std::copy_if(allChests.begin(), allChests.end(), std::back_inserter(filteredChests), [&](const ChestData& chest) {
        bool dimMatch =
            dimIdFilter.empty() || std::find(dimIdFilter.begin(), dimIdFilter.end(), chest.dimId) != dimIdFilter.end();
        bool typeMatch = chestTypeFilter.empty()
                      || std::find(chestTypeFilter.begin(), chestTypeFilter.end(), chest.type) != chestTypeFilter.end();

        bool coordMatch = true;
        if (coordRange.minX.has_value()) {
            coordMatch = (!coordRange.minX.has_value() || chest.pos.x >= *coordRange.minX)
                      && (!coordRange.maxX.has_value() || chest.pos.x <= *coordRange.maxX)
                      && (!coordRange.minY.has_value() || chest.pos.y >= *coordRange.minY)
                      && (!coordRange.maxY.has_value() || chest.pos.y <= *coordRange.maxY)
                      && (!coordRange.minZ.has_value() || chest.pos.z >= *coordRange.minZ)
                      && (!coordRange.maxZ.has_value() || chest.pos.z <= *coordRange.maxZ);
        }

        return dimMatch && typeMatch && coordMatch;
    });

    return filteredChests;
}

std::string escapeSnbtString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += ch; break;
        }
    }
    return escaped;
}

std::unique_ptr<CompoundTag> buildDisplayTag(const std::string& name, const std::vector<std::string>& loreLines) {
    std::string snbt = "{Name:\"" + escapeSnbtString(name) + "\",Lore:[";
    for (size_t i = 0; i < loreLines.size(); ++i) {
        if (i > 0) snbt += ",";
        snbt += "\"" + escapeSnbtString(loreLines[i]) + "\"";
    }
    snbt += "]}";
    return NbtUtils::parseSNBT(snbt);
}

bool applyDisplayTag(ItemStack& item, const std::string& name, const std::vector<std::string>& loreLines) {
    auto itemNbt = NbtUtils::getItemNbt(item);
    if (!itemNbt) return false;

    CompoundTag tagNbt;
    if (itemNbt->contains("tag")) {
        tagNbt = itemNbt->at("tag").get<CompoundTag>();
    }

    auto displayTag = buildDisplayTag(name, loreLines);
    if (!displayTag) return false;

    tagNbt["display"] = *displayTag;
    (*itemNbt)["tag"] = std::move(tagNbt);
    return NbtUtils::setItemNbt(item, *itemNbt);
}

ItemStack makeChestUiControlItem(std::string_view typeName, const std::string& displayName) {
    ItemStack item;
    item.reinit(typeName, 1, 0);
    applyDisplayTag(item, displayName, {});
    return item;
}

Player* findOnlinePlayer(const std::string& uuidString) {
    auto level = ll::service::getLevel();
    if (!level) return nullptr;
    auto uuid = mce::UUID::fromString(uuidString);
    if (uuid == mce::UUID::EMPTY()) return nullptr;
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

struct AdminChestUiPageData {
    std::string            title;
    std::vector<ItemStack> items;
    std::vector<ChestData> entries;
    int                    currentPage{1};
    int                    totalPages{1};
};

AdminChestUiPageData buildAdminChestUiPage(
    int                           requestedPage,
    const std::vector<int>&       dimIdFilter,
    const std::vector<ChestType>& chestTypeFilter,
    const CoordinateRange&        coordRange,
    bool                          includeBack
) {
    auto& txt       = TextService::getInstance();
    auto& i18n      = I18nService::getInstance();
    auto  allChests = collectFilteredAdminChests(dimIdFilter, chestTypeFilter, coordRange);

    auto pageSlice = Pagination::makeOneBasedPageSlice(
        static_cast<int>(allChests.size()),
        static_cast<int>(kAdminChestUiPageSize),
        requestedPage
    );

    std::vector<ItemStack> chestItems(54, ItemStack::EMPTY_ITEM());
    std::vector<ChestData> entries;
    entries.reserve(std::max(0, pageSlice.endIndex - pageSlice.startIndex));

    std::vector<std::string> uuids;
    for (int i = pageSlice.startIndex; i < pageSlice.endIndex; ++i) {
        uuids.push_back(allChests[i].ownerUuid);
    }
    auto ownerNameCache = CT::FormUtils::getPlayerNameCache(uuids);

    for (int i = pageSlice.startIndex; i < pageSlice.endIndex; ++i) {
        const auto& chest = allChests[i];
        std::string ownerName = ownerNameCache[chest.ownerUuid];
        if (ownerName.empty()) {
            ownerName = i18n.get("public_shop.unknown_owner");
        }

        ItemStack item;
        item.reinit("minecraft:chest", 1, 0);
        applyDisplayTag(
            item,
            ownerName + " §7[" + txt.getChestTypeName(chest.type) + "]§r",
            {
                CT::FormUtils::dimIdToString(chest.dimId),
                std::to_string(chest.pos.x) + ", " + std::to_string(chest.pos.y) + ", " + std::to_string(chest.pos.z),
                i18n.get("admin.select_chest_hint")
            }
        );
        chestItems[entries.size()] = std::move(item);
        entries.push_back(chest);
    }

    if (pageSlice.currentPage > 1) {
        chestItems[kAdminChestUiPrevSlot] = makeChestUiControlItem("minecraft:arrow", i18n.get("public_shop.button_prev_page"));
    }
    chestItems[kAdminChestUiInfoSlot] = makeChestUiControlItem(
        "minecraft:book",
        i18n.get(
            "form.chest_ui_page_info",
            {{"page", std::to_string(pageSlice.currentPage)}, {"total", std::to_string(pageSlice.totalPages)}}
        )
    );
    if (pageSlice.currentPage < pageSlice.totalPages) {
        chestItems[kAdminChestUiNextSlot] = makeChestUiControlItem("minecraft:arrow", i18n.get("public_shop.button_next_page"));
    }
    chestItems[kAdminChestUiFilterSlot] = makeChestUiControlItem("minecraft:comparator", i18n.get("admin.open_filter_form"));
    if (includeBack) {
        chestItems[kAdminChestUiBackSlot] = makeChestUiControlItem("minecraft:arrow", i18n.get("form.button_back"));
    }
    chestItems[kAdminChestUiCloseSlot] = makeChestUiControlItem("minecraft:barrier", i18n.get("public_shop.button_close"));

    return {
        .title       = i18n.get("admin.manage_title"),
        .items       = std::move(chestItems),
        .entries     = std::move(entries),
        .currentPage = pageSlice.currentPage,
        .totalPages  = pageSlice.totalPages
    };
}

void showAdminFilterForm(
    Player&                       player,
    const std::vector<int>&       dimIdFilter,
    const std::vector<ChestType>& chestTypeFilter,
    const CoordinateRange&        coordRange,
    std::function<void(Player&, const std::vector<int>&, const std::vector<ChestType>&, const CoordinateRange&)> onApply,
    std::function<void(Player&)> onCancel
) {
    auto&                i18n = I18nService::getInstance();
    auto&                txt  = TextService::getInstance();
    ll::form::CustomForm fm;
    fm.setTitle(i18n.get("admin.filter_title"));

    const bool allDimsSelected =
        dimIdFilter.empty() || (dimIdFilter.size() >= 3
                                && std::find(dimIdFilter.begin(), dimIdFilter.end(), 0) != dimIdFilter.end()
                                && std::find(dimIdFilter.begin(), dimIdFilter.end(), 1) != dimIdFilter.end()
                                && std::find(dimIdFilter.begin(), dimIdFilter.end(), 2) != dimIdFilter.end());
    const bool allTypesSelected = chestTypeFilter.empty();

    fm.appendHeader(i18n.get("admin.filter_by_dimension"));
    fm.appendToggle("dim_all", i18n.get("admin.all_dimensions"), allDimsSelected);
    fm.appendToggle("dim_0", i18n.get("dimension.overworld"), allDimsSelected || std::find(dimIdFilter.begin(), dimIdFilter.end(), 0) != dimIdFilter.end());
    fm.appendToggle("dim_1", i18n.get("dimension.nether"), allDimsSelected || std::find(dimIdFilter.begin(), dimIdFilter.end(), 1) != dimIdFilter.end());
    fm.appendToggle("dim_2", i18n.get("dimension.end"), allDimsSelected || std::find(dimIdFilter.begin(), dimIdFilter.end(), 2) != dimIdFilter.end());
    fm.appendDivider();

    fm.appendHeader(i18n.get("admin.filter_by_type"));
    fm.appendToggle("type_all", i18n.get("admin.all_types"), allTypesSelected);
    fm.appendToggle(
        "type_1",
        txt.getChestTypeName(ChestType::Locked),
        allTypesSelected || std::find(chestTypeFilter.begin(), chestTypeFilter.end(), ChestType::Locked) != chestTypeFilter.end()
    );
    fm.appendToggle(
        "type_2",
        txt.getChestTypeName(ChestType::RecycleShop),
        allTypesSelected
            || std::find(chestTypeFilter.begin(), chestTypeFilter.end(), ChestType::RecycleShop) != chestTypeFilter.end()
    );
    fm.appendToggle(
        "type_3",
        txt.getChestTypeName(ChestType::Shop),
        allTypesSelected || std::find(chestTypeFilter.begin(), chestTypeFilter.end(), ChestType::Shop) != chestTypeFilter.end()
    );
    fm.appendToggle(
        "type_4",
        txt.getChestTypeName(ChestType::Public),
        allTypesSelected || std::find(chestTypeFilter.begin(), chestTypeFilter.end(), ChestType::Public) != chestTypeFilter.end()
    );
    fm.appendDivider();

    fm.appendHeader(i18n.get("admin.filter_by_coords"));
    fm.appendInput("pos1", i18n.get("admin.start_coords"), i18n.get("admin.coords_example"), coordRangeToInput(coordRange, true));
    fm.appendInput("pos2", i18n.get("admin.end_coords"), i18n.get("admin.coords_example"), coordRangeToInput(coordRange, false));

    fm.sendTo(player, [dimIdFilter, chestTypeFilter, coordRange, onApply = std::move(onApply), onCancel = std::move(onCancel)](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
        if (!result.has_value() || result->empty()) {
            if (onCancel) {
                onCancel(p);
            }
            return;
        }

        std::vector<int> newDimIdFilter;
        if (!std::get<uint64>(result->at("dim_all"))) {
            if (std::get<uint64>(result->at("dim_0"))) newDimIdFilter.push_back(0);
            if (std::get<uint64>(result->at("dim_1"))) newDimIdFilter.push_back(1);
            if (std::get<uint64>(result->at("dim_2"))) newDimIdFilter.push_back(2);
        }

        std::vector<ChestType> newChestTypeFilter;
        if (!std::get<uint64>(result->at("type_all"))) {
            if (std::get<uint64>(result->at("type_1"))) newChestTypeFilter.push_back(ChestType::Locked);
            if (std::get<uint64>(result->at("type_2"))) newChestTypeFilter.push_back(ChestType::RecycleShop);
            if (std::get<uint64>(result->at("type_3"))) newChestTypeFilter.push_back(ChestType::Shop);
            if (std::get<uint64>(result->at("type_4"))) newChestTypeFilter.push_back(ChestType::Public);
        }

        CoordinateRange newCoordRange;
        auto            pos1Str = std::get<std::string>(result->at("pos1"));
        auto            pos2Str = std::get<std::string>(result->at("pos2"));
        auto            pos1Opt = parseCoordinates(pos1Str);
        auto            pos2Opt = parseCoordinates(pos2Str);

        if (pos1Opt && pos2Opt) {
            newCoordRange.minX = std::min(pos1Opt->x, pos2Opt->x);
            newCoordRange.maxX = std::max(pos1Opt->x, pos2Opt->x);
            newCoordRange.minY = std::min(pos1Opt->y, pos2Opt->y);
            newCoordRange.maxY = std::max(pos1Opt->y, pos2Opt->y);
            newCoordRange.minZ = std::min(pos1Opt->z, pos2Opt->z);
            newCoordRange.maxZ = std::max(pos1Opt->z, pos2Opt->z);
        } else if (pos1Opt && !pos2Opt) {
            newCoordRange.minX = newCoordRange.maxX = pos1Opt->x;
            newCoordRange.minY = newCoordRange.maxY = pos1Opt->y;
            newCoordRange.minZ = newCoordRange.maxZ = pos1Opt->z;
        }

        if (onApply) {
            onApply(p, newDimIdFilter, newChestTypeFilter, newCoordRange);
        }
    });
}

} // namespace


void showAdminMainForm(Player& player) {
    showAdminForm(player, 1, {}, {}, {});
}

void showAdminChestUi(
    Player&                       player,
    int                           currentPage,
    const std::vector<int>&       dimIdFilter,
    const std::vector<ChestType>& chestTypeFilter,
    const CoordinateRange&        coordRange,
    std::function<void(Player&)>  onBack
) {
    struct AdminChestUiState {
        int                    currentPage{1};
        std::vector<int>       dimIdFilter;
        std::vector<ChestType> chestTypeFilter;
        CoordinateRange        coordRange;
        std::function<void(Player&)> onBack;
    };

    auto state = std::make_shared<AdminChestUiState>(AdminChestUiState{
        .currentPage     = currentPage,
        .dimIdFilter     = dimIdFilter,
        .chestTypeFilter = chestTypeFilter,
        .coordRange      = coordRange,
        .onBack          = std::move(onBack)
    });

    auto refreshView = std::make_shared<std::function<void(Player&, bool)>>();
    *refreshView     = [state, refreshView](Player& target, bool reopen) {
        auto pageData = buildAdminChestUiPage(
            state->currentPage,
            state->dimIdFilter,
            state->chestTypeFilter,
            state->coordRange,
            static_cast<bool>(state->onBack)
        );
        state->currentPage = pageData.currentPage;

        auto handleClick = [state, refreshView](Player& p, CT::ChestUI::ClickContext const& ctx) {
            auto& txt = TextService::getInstance();
            switch (ctx.slot) {
            case kAdminChestUiPrevSlot:
                if (state->currentPage > 1) {
                    --state->currentPage;
                    (*refreshView)(p, false);
                }
                return;
            case kAdminChestUiNextSlot: {
                auto currentData = buildAdminChestUiPage(
                    state->currentPage,
                    state->dimIdFilter,
                    state->chestTypeFilter,
                    state->coordRange,
                    static_cast<bool>(state->onBack)
                );
                if (state->currentPage < currentData.totalPages) {
                    ++state->currentPage;
                    (*refreshView)(p, false);
                }
                return;
            }
            case kAdminChestUiFilterSlot:
                CT::ChestUI::close(p);
                runForOnlinePlayerAfterTicks(
                    p,
                    kAdminChestUiDelayTicks,
                    [state](Player& target) {
                        showAdminFilterForm(
                            target,
                            state->dimIdFilter,
                            state->chestTypeFilter,
                            state->coordRange,
                            [onBack = state->onBack](Player& playerToShow, const std::vector<int>& newDimIdFilter, const std::vector<ChestType>& newChestTypeFilter, const CoordinateRange& newCoordRange) {
                                showAdminChestUi(playerToShow, 1, newDimIdFilter, newChestTypeFilter, newCoordRange, onBack);
                            },
                            [state](Player& playerToShow) {
                                showAdminChestUi(
                                    playerToShow,
                                    state->currentPage,
                                    state->dimIdFilter,
                                    state->chestTypeFilter,
                                    state->coordRange,
                                    state->onBack
                                );
                            }
                        );
                    }
                );
                return;
            case kAdminChestUiBackSlot:
                if (state->onBack) {
                    state->onBack(p);
                }
                return;
            case kAdminChestUiCloseSlot:
                CT::ChestUI::close(p);
                return;
            default:
                break;
            }

            if (ctx.slot == kAdminChestUiInfoSlot || ctx.slot >= kAdminChestUiPageSize) {
                return;
            }

            auto currentData = buildAdminChestUiPage(
                state->currentPage,
                state->dimIdFilter,
                state->chestTypeFilter,
                state->coordRange,
                static_cast<bool>(state->onBack)
            );
            if (ctx.slot >= currentData.entries.size()) {
                return;
            }

            const auto& chest = currentData.entries[ctx.slot];
            p.teleport({chest.pos.x, chest.pos.y, chest.pos.z}, chest.dimId);
            p.sendMessage(txt.getMessage(
                "teleport.admin_success",
                {{"x", std::to_string(chest.pos.x)}, {"y", std::to_string(chest.pos.y)}, {"z", std::to_string(chest.pos.z)}}
            ));
        };

        if (reopen || !CT::ChestUI::isOpen(target)) {
            CT::ChestUI::OpenRequest request;
            request.title        = pageData.title;
            request.items        = pageData.items;
            request.onClick      = std::move(handleClick);
            request.onClose      = [](Player&) {};
            request.closeOnClick = false;
            if (!CT::ChestUI::open(target, std::move(request))) {
                showAdminForm(target, state->currentPage, state->dimIdFilter, state->chestTypeFilter, state->coordRange);
            }
            return;
        }

        CT::ChestUI::UpdateRequest request;
        request.title        = pageData.title;
        request.items        = pageData.items;
        request.onClick      = std::move(handleClick);
        request.onClose      = [](Player&) {};
        request.closeOnClick = false;
        if (!CT::ChestUI::update(target, std::move(request))) {
            (*refreshView)(target, true);
        }
    };

    (*refreshView)(player, false);
}


void showAdminForm(
    Player&                       player,
    int                           currentPage,
    const std::vector<int>&       dimIdFilter,
    const std::vector<ChestType>& chestTypeFilter,
    const CoordinateRange&        coordRange
) {
    auto&                i18n = I18nService::getInstance();
    ll::form::CustomForm fm;
    fm.setTitle(i18n.get("admin.manage_title"));

    auto                   allChests = ChestRepository::getInstance().findAll();
    std::vector<ChestData> filteredChests;

    // 应用筛选
    std::copy_if(allChests.begin(), allChests.end(), std::back_inserter(filteredChests), [&](const ChestData& chest) {
        bool dimMatch =
            dimIdFilter.empty() || std::find(dimIdFilter.begin(), dimIdFilter.end(), chest.dimId) != dimIdFilter.end();
        bool typeMatch = chestTypeFilter.empty()
                      || std::find(chestTypeFilter.begin(), chestTypeFilter.end(), chest.type) != chestTypeFilter.end();

        bool coordMatch = true;
        if (coordRange.minX.has_value()) { // 只要有一个坐标值，就进行判断
            coordMatch = (!coordRange.minX.has_value() || chest.pos.x >= *coordRange.minX)
                      && (!coordRange.maxX.has_value() || chest.pos.x <= *coordRange.maxX)
                      && (!coordRange.minY.has_value() || chest.pos.y >= *coordRange.minY)
                      && (!coordRange.maxY.has_value() || chest.pos.y <= *coordRange.maxY)
                      && (!coordRange.minZ.has_value() || chest.pos.z >= *coordRange.minZ)
                      && (!coordRange.maxZ.has_value() || chest.pos.z <= *coordRange.maxZ);
        }

        return dimMatch && typeMatch && coordMatch;
    });

    const int itemsPerPage = 10;
    auto      pageSlice    = Pagination::makeOneBasedPageSlice(
        static_cast<int>(filteredChests.size()),
        itemsPerPage,
        currentPage
    );
    const int totalPages   = pageSlice.totalPages;
    currentPage            = pageSlice.currentPage;

    fm.appendToggle(
        "open_filter_form",
        i18n.get("admin.open_filter_form"),
        false,
        i18n.get("admin.open_filter_form_hint")
    );
    fm.appendToggle(
        "confirm_teleport",
        i18n.get("admin.direct_teleport"),
        false,
        i18n.get("admin.direct_teleport_hint")
    );

    if (filteredChests.empty()) {
        fm.appendLabel(i18n.get("admin.no_chests_found"));
    } else {
        if (totalPages > 1) {
            fm.appendSlider(
                "page_slider",
                i18n.get("admin.page_slider"),
                1,
                totalPages,
                1,
                currentPage,
                i18n.get(
                    "admin.page_slider_hint",
                    {
                        {"current", std::to_string(currentPage)},
                        {"total",   std::to_string(totalPages) }
            }
                )
            );
        }

        std::map<std::string, std::vector<ChestData>> playerChests;
        for (const auto& chest : filteredChests) {
            playerChests[chest.ownerUuid].push_back(chest);
        }

        fm.appendHeader(i18n.get(
            "admin.chest_list_header",
            {
                {"total",       std::to_string(filteredChests.size())},
                {"current",     std::to_string(currentPage)          },
                {"total_pages", std::to_string(totalPages)           }
        }
        ));
        fm.appendDivider();

        int startIndex = pageSlice.startIndex;
        int endIndex   = pageSlice.endIndex;

        std::vector<ChestData> pagedChests;
        for (const auto& pair : playerChests) {
            pagedChests.insert(pagedChests.end(), pair.second.begin(), pair.second.end());
        }

        // 预先批量查询当前页所有玩家名称，避免 N+1 查询
        std::vector<std::string> uuids;
        for (int i = startIndex; i < endIndex; ++i) {
            uuids.push_back(pagedChests[i].ownerUuid);
        }
        auto ownerNameCache = CT::FormUtils::getPlayerNameCache(uuids);

        auto& txt = TextService::getInstance();
        for (int i = startIndex; i < endIndex; ++i) {
            const auto&        chest     = pagedChests[i];
            const std::string& ownerName = ownerNameCache[chest.ownerUuid];

            std::string label = "§b" + ownerName + " §f- " + CT::FormUtils::dimIdToString(chest.dimId) + " §7- "
                              + txt.getChestTypeName(chest.type) + " §r§e[" + std::to_string(chest.pos.x) + ", "
                              + std::to_string(chest.pos.y) + ", " + std::to_string(chest.pos.z) + "]";

            std::string toggleName = chest.ownerUuid + "|" + std::to_string(chest.dimId) + "|"
                                   + std::to_string(chest.pos.x) + "|" + std::to_string(chest.pos.y) + "|"
                                   + std::to_string(chest.pos.z);

            fm.appendToggle(toggleName, label, false, i18n.get("admin.select_chest_hint"));
        }
        fm.appendDivider();
    }

    fm.sendTo(
        player,
        [dimIdFilter,
         chestTypeFilter,
         coordRange](Player& p, const ll::form::CustomFormResult& result, ll::form::FormCancelReason) {
            auto& i18n = I18nService::getInstance();
            if (!result.has_value() || result->empty()) {
                p.sendMessage(i18n.get("admin.manage_closed"));
                return;
            }

            int currentResultPage = 1;
            if (result->count("page_slider")) {
                currentResultPage = static_cast<int>(std::get<double>(result->at("page_slider")));
            }

            if (result->count("open_filter_form") && std::get<uint64>(result->at("open_filter_form")) == 1) {
                showAdminFilterForm(
                    p,
                    dimIdFilter,
                    chestTypeFilter,
                    coordRange,
                    [](Player& target, const std::vector<int>& newDimIdFilter, const std::vector<ChestType>& newChestTypeFilter, const CoordinateRange& newCoordRange) {
                        showAdminForm(target, 1, newDimIdFilter, newChestTypeFilter, newCoordRange);
                    },
                    [dimIdFilter, chestTypeFilter, coordRange, currentResultPage](Player& target) {
                        showAdminForm(target, currentResultPage, dimIdFilter, chestTypeFilter, coordRange);
                    }
                );
                return;
            }

            bool confirmTeleport = false;
            if (result->count("confirm_teleport") && std::get<uint64>(result->at("confirm_teleport")) == 1) {
                confirmTeleport = true;
            }

            std::string selectedChestToggle;
            for (const auto& [toggleName, value] : *result) {
                if (toggleName.rfind("dim_", 0) != 0 && toggleName.rfind("type_", 0) != 0
                    && toggleName != "confirm_teleport" && toggleName != "page_slider" && toggleName != "pos1"
                    && toggleName != "pos2") {
                    if (value.index() == 1 && std::get<uint64>(value) == 1) {
                        selectedChestToggle = toggleName;
                        break;
                    }
                }
            }

            if (confirmTeleport && !selectedChestToggle.empty()) {
                std::stringstream        ss(selectedChestToggle);
                std::string              segment;
                std::vector<std::string> parts;
                while (std::getline(ss, segment, '|')) {
                    parts.push_back(segment);
                }

                if (parts.size() == 5) {
                    try {
                        auto& txt = TextService::getInstance();

                        int dimId = std::stoi(parts[1]);
                        int x     = std::stoi(parts[2]);
                        int y     = std::stoi(parts[3]);
                        int z     = std::stoi(parts[4]);

                        // 管理员传送不受任何限制（不收费、不冷却）
                        p.teleport({x, y, z}, dimId);

                        p.sendMessage(txt.getMessage(
                            "teleport.admin_success",
                            {
                                {"x", std::to_string(x)},
                                {"y", std::to_string(y)},
                                {"z", std::to_string(z)}
                        }
                        ));
                        return;
                    } catch (const std::exception&) {
                        p.sendMessage(i18n.get("admin.teleport_failed"));
                        return;
                    }
                }
            }

            if (result->count("page_slider")) {
                showAdminForm(p, currentResultPage, dimIdFilter, chestTypeFilter, coordRange);
            } else {
                showAdminForm(p, 1, dimIdFilter, chestTypeFilter, coordRange);
            }
        }
    );
}

} // namespace CT
