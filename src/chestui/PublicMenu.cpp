#include "chestui/PublicMenu.h"

#include "Utils/NbtUtils.h"
#include "chestui/chestui.h"
#include "compat/PermissionCompat.h"
#include "form/AdminForm.h"
#include "form/PublicItemsForm.h"
#include "form/PublicShopForm.h"
#include "form/SalesRankingForm.h"
#include "form/TradeRecordForm.h"
#include "ll/api/service/Bedrock.h"
#include "ll/api/thread/ServerThreadExecutor.h"
#include "mc/platform/UUID.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"
#include "service/TextService.h"

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace CT::ChestUI::PublicMenu {

namespace {

constexpr size_t kMenuSlotCount           = 54;
constexpr size_t kMenuInfoSlot            = 4;
constexpr size_t kMenuPublicShopsSlot     = 10;
constexpr size_t kMenuPublicItemsSlot     = 12;
constexpr size_t kMenuRecycleShopsSlot    = 14;
constexpr size_t kMenuRecycleItemsSlot    = 16;
constexpr size_t kMenuRankingSlot         = 28;
constexpr size_t kMenuRecordsSlot         = 30;
constexpr size_t kMenuAdminSlot           = 32;
constexpr size_t kMenuCloseSlot           = 49;

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

std::unique_ptr<CompoundTag> buildDisplayTag(const std::string& name, const std::vector<std::string>& loreLines) {
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

bool applyDisplayTag(ItemStack& item, const std::string& name, const std::vector<std::string>& loreLines) {
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

ItemStack makeMenuItem(std::string_view typeName, const std::string& displayName, const std::vector<std::string>& loreLines = {}) {
    ItemStack item;
    item.reinit(typeName, 1, 0);
    applyDisplayTag(item, displayName, loreLines);
    return item;
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

std::vector<ItemStack> buildMenuItems(Player& player) {
    auto& txt = TextService::getInstance();
    bool  isAdmin = PermissionCompat::hasPermission(player.getUuid().asString(), "chest.admin");

    std::vector<ItemStack> items(kMenuSlotCount, ItemStack::EMPTY_ITEM());
    items[kMenuInfoSlot] = makeMenuItem(
        "minecraft:nether_star",
        txt.getMessage("form.public_hub_title"),
        {txt.getMessage("form.public_hub_info")}
    );
    items[kMenuPublicShopsSlot] = makeMenuItem(
        "minecraft:chest",
        txt.getMessage("form.public_hub_shops"),
        {txt.getMessage("form.public_hub_shops_lore")}
    );
    items[kMenuPublicItemsSlot] = makeMenuItem(
        "minecraft:emerald",
        txt.getMessage("form.public_hub_items"),
        {txt.getMessage("form.public_hub_items_lore")}
    );
    items[kMenuRecycleShopsSlot] = makeMenuItem(
        "minecraft:hopper",
        txt.getMessage("form.public_hub_recycle_shops"),
        {txt.getMessage("form.public_hub_recycle_shops_lore")}
    );
    items[kMenuRecycleItemsSlot] = makeMenuItem(
        "minecraft:iron_ingot",
        txt.getMessage("form.public_hub_recycle_items"),
        {txt.getMessage("form.public_hub_recycle_items_lore")}
    );
    items[kMenuRankingSlot] = makeMenuItem(
        "minecraft:gold_ingot",
        txt.getMessage("form.public_hub_ranking"),
        {txt.getMessage("form.public_hub_ranking_lore")}
    );
    items[kMenuRecordsSlot] = makeMenuItem(
        "minecraft:book",
        txt.getMessage("form.public_hub_records"),
        {txt.getMessage("form.public_hub_records_lore")}
    );
    if (isAdmin) {
        items[kMenuAdminSlot] = makeMenuItem(
            "minecraft:barrel",
            txt.getMessage("form.public_hub_admin"),
            {txt.getMessage("form.public_hub_admin_lore")}
        );
    }
    items[kMenuCloseSlot] = makeMenuItem("minecraft:barrier", txt.getMessage("public_shop.button_close"));
    return items;
}

} // namespace

bool open(Player& player) {
    auto& txt = TextService::getInstance();

    auto handleClick = [](Player& p, ChestUI::ClickContext const& ctx) {
        switch (ctx.slot) {
        case kMenuPublicShopsSlot:
            showPublicShopListChestUi(p, 0, "", "owner", OfficialFilter::All, PublicListSortMode::Sales, [](Player& backPlayer) {
                open(backPlayer);
            });
            return;
        case kMenuPublicItemsSlot:
            showPublicItemsChestUi(p, 0, "", [](Player& backPlayer) { open(backPlayer); });
            return;
        case kMenuRecycleShopsSlot:
            showPublicRecycleShopListChestUi(
                p,
                0,
                "",
                "owner",
                OfficialFilter::All,
                PublicListSortMode::Sales,
                [](Player& backPlayer) { open(backPlayer); }
            );
            return;
        case kMenuRecycleItemsSlot:
            showPublicRecycleItemsChestUi(p, 0, "", [](Player& backPlayer) { open(backPlayer); });
            return;
        case kMenuRankingSlot:
            ChestUI::close(p);
            runForOnlinePlayerAfterTicks(p, 2, [](Player& target) { showSalesRankingForm(target); });
            return;
        case kMenuRecordsSlot:
            ChestUI::close(p);
            runForOnlinePlayerAfterTicks(p, 2, [](Player& target) {
                showTradeRecordMenuForm(target, [](Player& backPlayer) { open(backPlayer); });
            });
            return;
        case kMenuAdminSlot:
            if (!PermissionCompat::hasPermission(p.getUuid().asString(), "chest.admin")) {
                p.sendMessage(TextService::getInstance().getMessage("command.no_permission"));
                return;
            }
            showAdminChestUi(p, 1, {}, {}, {}, [](Player& backPlayer) { open(backPlayer); });
            return;
        case kMenuCloseSlot:
            ChestUI::close(p);
            return;
        default:
            return;
        }
    };

    auto items = buildMenuItems(player);
    auto title = txt.getMessage("form.public_hub_title");

    if (ChestUI::isOpen(player)) {
        ChestUI::UpdateRequest request;
        request.title        = std::move(title);
        request.items        = std::move(items);
        request.onClick      = std::move(handleClick);
        request.onClose      = [](Player&) {};
        request.closeOnClick = false;
        return ChestUI::update(player, std::move(request));
    }

    ChestUI::OpenRequest request;
    request.title        = std::move(title);
    request.items        = std::move(items);
    request.onClick      = std::move(handleClick);
    request.closeOnClick = false;
    request.onClose      = [](Player&) {};
    return ChestUI::open(player, std::move(request));
}

} // namespace CT::ChestUI::PublicMenu
