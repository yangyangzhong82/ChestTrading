#pragma once

#include <functional>

class Player;

namespace CT::ChestUI::PublicMenu {

bool open(Player& player, std::function<void(Player&)> onBack = {});

} // namespace CT::ChestUI::PublicMenu
