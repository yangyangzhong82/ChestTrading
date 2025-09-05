#include "db/Sqlite3Wrapper.h" // 引入 Sqlite3Wrapper
#include "interaction/chestprotect.h"
#include "ll/api/form/SimpleForm.h"
#include "logger.h"
#include "mc/platform/UUID.h"
#include "LockForm.h"

namespace CT {
void showChestLockForm(Player& player, BlockPos pos, int dimId) {
    ll::form::SimpleForm fm("上锁箱子", "你确定要上锁这个箱子吗？");

    fm.appendButton("确定", [pos, dimId](Player& p) {
        logger.info(
            "玩家 {} 选择上锁位于维度 {} 的 ({}, {}, {}) 的箱子。",
            p.getUuid().asString(),
            dimId,
            pos.x,
            pos.y,
            pos.z
        );
        if (lockChest(p.getUuid().asString(), pos, dimId)) {
            logger.info("箱子信息已存入数据库。");
        } else {
            logger.error("箱子信息存入数据库失败。");
        }
    });

    fm.appendButton("取消", [](Player& p) { logger.info("玩家 {} 取消了上锁操作。", p.getUuid().asString()); });

    fm.sendTo(player);
}
}