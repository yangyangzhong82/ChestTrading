#pragma once

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"

#include <cstdint>
#include <optional>

namespace CT {

class PLandCompat {
public:
    static PLandCompat& getInstance();

    void probe();

    std::optional<bool> isInLand(Player const& player, BlockPos const& pos) const;
    std::optional<bool> isOwnerLand(std::string const& playerUuid, BlockPos const& pos, int dimId) const;

    // 判断领地环境是否允许漏斗（含漏斗矿车）从容器中吸取物品。
    // PLand 不可用或位置不在领地内时返回 true，不影响未接入 PLand 的服务器。
    bool canHopperPullItems(BlockPos const& pos, int dimId) const;

    /**
     * @brief 获取指定位置所在领地的 ID。
     * @return std::nullopt 表示 PLand 不可用；-1 表示该位置不在任何领地内；否则为领地 ID。
     */
    std::optional<int64_t> getLandId(BlockPos const& pos, int dimId) const;

    /**
     * @brief 判断玩家是否为该位置所在领地的管理者（领地主人或 PLand 操作员）。
     * @return std::nullopt 表示 PLand 不可用；false 表示不在领地内或无管理权限；true 表示可管理。
     */
    std::optional<bool> isLandManager(Player const& player, BlockPos const& pos) const;

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
