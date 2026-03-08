#include "chestui/demo.h"

#include "chestui/chestui.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/inventory/network/ItemStackRequestActionType.h"
#include "mc/world/item/ItemStack.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

namespace CT::ChestUI::Demo {

namespace {

constexpr size_t kPageSize    = 21;
constexpr size_t kPrevSlot    = 21;
constexpr size_t kInfoSlot    = 22;
constexpr size_t kNextSlot    = 23;
constexpr size_t kRefreshSlot = 24;
constexpr size_t kCloseSlot   = 26;

struct DemoState {
    std::vector<ItemStack> catalog;
    std::vector<int>       clickCounts;
    size_t                 currentPage{0};
};

auto actionTypeToString(ItemStackRequestActionType type) -> std::string {
    switch (type) {
    case ItemStackRequestActionType::Take:
        return "Take";
    case ItemStackRequestActionType::Place:
        return "Place";
    case ItemStackRequestActionType::Swap:
        return "Swap";
    case ItemStackRequestActionType::Drop:
        return "Drop";
    case ItemStackRequestActionType::Destroy:
        return "Destroy";
    case ItemStackRequestActionType::Consume:
        return "Consume";
    case ItemStackRequestActionType::Create:
        return "Create";
    default:
        return std::to_string(static_cast<int>(type));
    }
}

auto makeItem(std::string_view typeName, int count) -> ItemStack {
    ItemStack item;
    item.reinit(typeName, count, 0);
    return item;
}

} // namespace

bool openPagedDemo(Player& player) {
    auto state = std::make_shared<DemoState>();
    std::vector<std::string_view> itemTypes = {
        "minecraft:diamond",           "minecraft:emerald",         "minecraft:gold_ingot",
        "minecraft:iron_ingot",        "minecraft:coal",            "minecraft:redstone",
        "minecraft:lapis_lazuli",      "minecraft:amethyst_shard",  "minecraft:copper_ingot",
        "minecraft:netherite_ingot",   "minecraft:apple",           "minecraft:bread",
        "minecraft:arrow",             "minecraft:clock",           "minecraft:compass",
        "minecraft:book",              "minecraft:paper",           "minecraft:slime_ball",
        "minecraft:ender_pearl",       "minecraft:blaze_rod",       "minecraft:glowstone_dust",
        "minecraft:quartz",            "minecraft:prismarine_shard","minecraft:nautilus_shell",
        "minecraft:experience_bottle", "minecraft:honey_bottle",    "minecraft:pumpkin_pie",
        "minecraft:cooked_beef",       "minecraft:golden_carrot",   "minecraft:melon_slice",
        "minecraft:cookie",            "minecraft:baked_potato",    "minecraft:firework_rocket",
        "minecraft:snowball",          "minecraft:phantom_membrane","minecraft:ghast_tear"
    };

    state->catalog.reserve(itemTypes.size());
    state->clickCounts.assign(itemTypes.size(), 0);
    for (size_t i = 0; i < itemTypes.size(); ++i) {
        state->catalog.push_back(makeItem(itemTypes[i], static_cast<int>((i % 8) + 1)));
    }

    auto getPageCount = [state]() -> size_t {
        return std::max<size_t>(1, (state->catalog.size() + kPageSize - 1) / kPageSize);
    };

    auto buildPageItems = [state, getPageCount]() -> std::vector<ItemStack> {
        std::vector<ItemStack> pageItems(27, ItemStack::EMPTY_ITEM());
        size_t                 pageCount = getPageCount();
        size_t                 start     = state->currentPage * kPageSize;
        size_t                 end       = std::min(start + kPageSize, state->catalog.size());
        for (size_t index = start; index < end; ++index) {
            pageItems[index - start] = state->catalog[index];
        }

        if (state->currentPage > 0) {
            pageItems[kPrevSlot] = makeItem("minecraft:arrow", 1);
        }
        pageItems[kInfoSlot] = makeItem("minecraft:book", static_cast<int>(std::min<size_t>(pageCount, 64)));
        if (state->currentPage + 1 < pageCount) {
            pageItems[kNextSlot] = makeItem("minecraft:arrow", 1);
        }
        pageItems[kRefreshSlot] = makeItem("minecraft:clock", 1);
        pageItems[kCloseSlot]   = makeItem("minecraft:barrier", 1);
        return pageItems;
    };

    auto makeTitle = [state, getPageCount]() -> std::string {
        auto totalClicks = std::accumulate(state->clickCounts.begin(), state->clickCounts.end(), 0);
        return "ChestUI Page Demo §7("
               + std::to_string(state->currentPage + 1) + "/" + std::to_string(getPageCount())
               + " | total clicks: " + std::to_string(totalClicks) + ")§r";
    };

    auto refreshView = std::make_shared<std::function<void(Player&)>>();
    *refreshView     = [state, buildPageItems, makeTitle](Player& target) {
        UpdateRequest request;
        request.title = makeTitle();
        request.items = buildPageItems();
        CT::ChestUI::update(target, std::move(request));
        target.refreshInventory();
    };

    OpenRequest request;
    request.title        = makeTitle();
    request.items        = buildPageItems();
    request.closeOnClick = false;
    request.onClick      = [state, refreshView, getPageCount](Player& p, ClickContext const& ctx) {
        if (ctx.slot == kCloseSlot) {
            close(p);
            return;
        }

        if (ctx.slot == kPrevSlot) {
            if (state->currentPage > 0) {
                --state->currentPage;
                (*refreshView)(p);
            }
            return;
        }

        if (ctx.slot == kNextSlot) {
            if (state->currentPage + 1 < getPageCount()) {
                ++state->currentPage;
                (*refreshView)(p);
            }
            return;
        }

        if (ctx.slot == kRefreshSlot) {
            (*refreshView)(p);
            p.sendMessage("§7[ChestUI] 已刷新当前分页。");
            return;
        }

        if (ctx.slot >= kPageSize) {
            return;
        }

        size_t globalIndex = state->currentPage * kPageSize + ctx.slot;
        if (globalIndex >= state->catalog.size() || ctx.item.isNull()) {
            return;
        }

        auto& item = state->catalog[globalIndex];
        item.mCount = static_cast<decltype(item.mCount)>(std::min(64, static_cast<int>(item.mCount) + 1));
        ++state->clickCounts[globalIndex];
        (*refreshView)(p);

        p.sendMessage(
            "§a[ChestUI] slot=" + std::to_string(ctx.slot)
            + ", action=" + actionTypeToString(ctx.actionType)
            + ", item=" + item.getTypeName()
            + ", newCount=" + std::to_string(item.mCount)
        );
    };
    request.onClose      = [](Player&) {};

    return open(player, std::move(request));
}

} // namespace CT::ChestUI::Demo
