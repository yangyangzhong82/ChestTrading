#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"
#include "logger.h"
#include "interaction/chestprotect.h"
#include "db/Sqlite3Wrapper.h" // 引入 Sqlite3Wrapper
#include "mc/platform/UUID.h"   // 引入 UUID
#include "event.h"

namespace CT {


void registerEventListener() {
    auto& eventBus = ll::event::EventBus::getInstance();

    eventBus.emplaceListener<ll::event::PlayerInteractBlockEvent>(
        [](ll::event::PlayerInteractBlockEvent& ev) {
            logger.info("PlayerInteractBlockEvent开始");
            auto& player = ev.self();
            auto& item   = player.getCarriedItem();
            auto block = ev.block();
            auto dimId = player.getDimensionId();
            auto pos = ev.blockPos();

            if (block->getTypeName() == "minecraft:chest") {
                Sqlite3Wrapper& db = Sqlite3Wrapper::getInstance();
                std::vector<std::vector<std::string>> results = db.query(
                    "SELECT player_uuid FROM locked_chests WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;",
                    static_cast<int>(dimId),
                    pos.x,
                    pos.y,
                    pos.z
                );

                logger.info(
                    "查询箱子锁定状态: 维度={}, 位置=({}, {}, {}), 结果数量={}",
                    static_cast<int>(dimId),
                    pos.x,
                    pos.y,
                    pos.z,
                    results.size()
                );

                if (!results.empty()) {
                    // 箱子已被锁定
                    std::string owner_uuid = results[0][0];
                    std::string current_player_uuid = player.getUuid().asString();

                    if (owner_uuid != current_player_uuid) {
                        // 玩家不是箱子主人，拦截事件
                        ev.cancel();
                        player.sendMessage("§c这个箱子已经被锁定了，你不是它的主人！");
                        logger.info(
                            "玩家 {} 尝试打开被玩家 {} 锁定的箱子，位于维度 {} 的 ({}, {}, {})。",
                            current_player_uuid,
                            owner_uuid,
                            static_cast<int>(dimId),
                            pos.x,
                            pos.y,
                            pos.z
                        );
                        return; // 阻止后续逻辑执行
                    } else {
                        // 玩家是箱子主人
                        if (item.getTypeName() == "minecraft:stick") {
                            // 主人手持木棍，显示上锁表单，并拦截默认打开行为
                            showChestLockForm(player, pos, static_cast<int>(dimId));
                            ev.cancel();
                            return; // 阻止后续逻辑执行
                        }
                        // 主人没有手持木棍，允许打开箱子 (不调用 ev.cancel())
                    }
                } else {
                    // 箱子未被锁定
                    if (item.getTypeName() == "minecraft:stick") {
                        // 手持木棍，显示上锁表单，并拦截默认打开行为
                        showChestLockForm(player, pos, static_cast<int>(dimId));
                        ev.cancel();
                        return; // 阻止后续逻辑执行
                    }
                    // 未锁定且没有手持木棍，允许打开箱子 (不调用 ev.cancel())
                }
            }
        }
    );
}

} // namespace CT
