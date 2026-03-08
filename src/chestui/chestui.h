#pragma once

#include "mc/world/inventory/network/ItemStackRequestActionType.h"
#include "mc/world/item/ItemStack.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class Player;

namespace CT::ChestUI {

struct ClickContext {
    uint8_t                    slot{};
    ItemStackRequestActionType actionType{ItemStackRequestActionType::Take};
    ItemStack                  item{};
};

using ClickCallback = std::function<void(Player&, ClickContext const&)>;
using CloseCallback = std::function<void(Player&)>;

struct OpenRequest {
    std::string            title;
    std::vector<ItemStack> items;
    ClickCallback          onClick;
    CloseCallback          onClose;
    int                    containerId{-30};
    bool                   closeOnClick{true};
};

struct UpdateRequest {
    std::optional<std::string> title;
    std::vector<ItemStack>     items;
};

bool open(Player& player, OpenRequest request);
bool update(Player& player, UpdateRequest request);
bool close(Player& player);
bool isOpen(Player const& player);
void closeAll();

} // namespace CT::ChestUI
