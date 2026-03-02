#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"

namespace CT {

class PLandCompat {
public:
    static PLandCompat& getInstance();

    void probe();

    bool canUseContainer(Player const& player, BlockPos const& pos) const;
    bool canPlace(Player const& player, BlockPos const& pos) const;
    bool canDestroy(Player const& player, BlockPos const& pos) const;

private:
    enum class Action {
        UseContainer,
        Place,
        Destroy
    };

    bool canPlayerDo(Player const& player, BlockPos const& pos, Action action) const;
};

} // namespace CT
